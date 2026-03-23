#!/usr/bin/env node
/**
 * Convert SVG icons to:
 * 1. C header with PROGMEM bitmap arrays (for small UI icons)
 * 2. Binary .bin files for LittleFS (for weather icons)
 *
 * Uses sharp for SVG → 1-bit bitmap conversion.
 */

const fs = require('fs');
const path = require('path');
const sharp = require('sharp');

const BASE_DIR = path.join(__dirname, '..');
const ASSETS_DIR = path.join(BASE_DIR, 'assets');
const QWEATHER_DIR = path.join(BASE_DIR, 'QWeather-Icons-1.8.0', 'icons'); // adjust if moved
const DATA_ICONS_DIR = path.join(BASE_DIR, 'data', 'icons');
const INCLUDE_DIR = path.join(BASE_DIR, 'include');

/**
 * Convert SVG to 1-bit XBM-style bitmap bytes.
 * Returns Buffer of bytes in XBM format (LSB first, rows byte-aligned).
 */
async function svgToBitmap(svgPath, width, height, useDithering = false) {
    // Read SVG and render
    const svgBuf = fs.readFileSync(svgPath);
    // 1. Flatten into white background first to avoid alpha/outline artifacts
    const rawBuf = await sharp(svgBuf, { density: 300 })
        .resize(width, height, { fit: 'contain', background: { r: 255, g: 255, b: 255, alpha: 1 } })
        .flatten({ background: { r: 255, g: 255, b: 255 } })
        .raw()
        .toBuffer();

    // rawBuf has 3 bytes per pixel: R, G, B
    const bytesPerRow = Math.ceil(width / 8);
    const bitmapBytes = Buffer.alloc(bytesPerRow * height, 0);
    
    // Create a 2D array for grayscale values to apply dithering
    const grayData = new Float32Array(width * height);
    for (let i = 0; i < width * height; i++) {
        const r = rawBuf[i * 3];
        const g = rawBuf[i * 3 + 1];
        const b = rawBuf[i * 3 + 2];
        // Standard luminance
        grayData[i] = 0.299 * r + 0.587 * g + 0.114 * b;
    }

    if (useDithering) {
        // Floyd-Steinberg dithering
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const i = y * width + x;
                const oldPixel = grayData[i];
                
                // Threshold at 128
                const newPixel = oldPixel < 128 ? 0 : 255;
                const quantError = oldPixel - newPixel;

                // Pixel is "on" (black) if newPixel is 0
                if (newPixel === 0) {
                    const byteIdx = y * bytesPerRow + Math.floor(x / 8);
                    const bitIdx = x % 8;
                    bitmapBytes[byteIdx] |= (1 << bitIdx); // XBM: LSB first
                }

                // Distribute error to neighboring pixels (Floyd-Steinberg)
                if (x + 1 < width) {
                    grayData[y * width + (x + 1)] += quantError * 7 / 16;
                }
                if (y + 1 < height) {
                    if (x - 1 >= 0) {
                        grayData[(y + 1) * width + (x - 1)] += quantError * 3 / 16;
                    }
                    grayData[(y + 1) * width + x] += quantError * 5 / 16;
                    if (x + 1 < width) {
                        grayData[(y + 1) * width + (x + 1)] += quantError * 1 / 16;
                    }
                }
            }
        }
    } else {
        // Simple thresholding without dithering
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const i = y * width + x;
                const gray = grayData[i];
                
                // Pixel is "on" (black) if it's less than 128
                if (gray < 128) {
                    const byteIdx = y * bytesPerRow + Math.floor(x / 8);
                    const bitIdx = x % 8;
                    bitmapBytes[byteIdx] |= (1 << bitIdx); // XBM: LSB first
                }
            }
        }
    }

    return bitmapBytes;
}

/**
 * Generate C PROGMEM array string.
 */
function generateCArray(name, data, width, height) {
    const lines = [];
    lines.push(`// ${name}: ${width}x${height}`);
    lines.push(`#define ICON_${name.toUpperCase()}_W ${width}`);
    lines.push(`#define ICON_${name.toUpperCase()}_H ${height}`);
    lines.push(`const uint8_t icon_${name}[] PROGMEM = {`);

    for (let i = 0; i < data.length; i += 12) {
        const chunk = [];
        for (let j = i; j < Math.min(i + 12, data.length); j++) {
            chunk.push(`0x${data[j].toString(16).padStart(2, '0')}`);
        }
        const suffix = (i + 12 < data.length) ? ',' : '';
        lines.push(`  ${chunk.join(', ')}${suffix}`);
    }

    lines.push('};');
    return lines.join('\n');
}

async function main() {
    // ===== 1. Small UI icons → C header =====
    const uiIcons = [
        { name: 'power', svg: path.join(ASSETS_DIR, 'power.svg'), w: 16, h: 16 },
        { name: 'house', svg: path.join(ASSETS_DIR, 'house.svg'), w: 18, h: 18 },
        { name: 'tree', svg: path.join(ASSETS_DIR, 'tree.svg'), w: 18, h: 18 },
        { name: 'temperature', svg: path.join(ASSETS_DIR, 'temperature.svg'), w: 16, h: 16 },
        { name: 'humidity', svg: path.join(ASSETS_DIR, 'humidity.svg'), w: 16, h: 16, dither: true },
        { name: 'local', svg: path.join(ASSETS_DIR, 'local.svg'), w: 16, h: 16 },
        { name: 'update', svg: path.join(ASSETS_DIR, 'update.svg'), w: 16, h: 16 },
    ];

    const headerParts = [
        '#ifndef __ICONS_H__',
        '#define __ICONS_H__',
        '',
        '#include <Arduino.h>',
        '#include <pgmspace.h>',
        '',
    ];

    for (const icon of uiIcons) {
        console.log(`Converting UI icon: ${icon.name} (${icon.w}x${icon.h})`);
        const useDither = icon.dither || false;
        const data = await svgToBitmap(icon.svg, icon.w, icon.h, useDither); 
        headerParts.push(generateCArray(icon.name, data, icon.w, icon.h));
        headerParts.push('');
    }

    headerParts.push('#endif // __ICONS_H__');
    headerParts.push('');

    const headerPath = path.join(INCLUDE_DIR, 'icons.h');
    fs.writeFileSync(headerPath, headerParts.join('\n'));
    console.log(`Generated: ${headerPath}`);

    // ===== 2. Weather icons → LittleFS binary files =====
    const weatherCodes = [
        // 晴/多云/阴
        100, 101, 102, 103, 104, 150, 151, 152, 153,
        // 雨
        300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310,
        311, 312, 313, 314, 315, 316, 317, 318, 350, 351, 399,
        // 雪
        400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410,
        456, 457, 499,
        // 雾/霾/沙尘
        500, 501, 502, 503, 504, 507, 508, 509, 510, 511, 512,
        513, 514, 515,
        // 温度/未知
        900, 901, 999,
    ];

    const sizes = [48, 24]; // 今日大图, 明/后日小图

    fs.mkdirSync(DATA_ICONS_DIR, { recursive: true });

    let converted = 0;
    let skipped = 0;

    for (const code of weatherCodes) {
        const svgPath = path.join(QWEATHER_DIR, `${code}.svg`);
        if (!fs.existsSync(svgPath)) {
            console.log(`  WARNING: ${code}.svg not found, skipping`);
            skipped++;
            continue;
        }

        for (const size of sizes) {
            console.log(`Converting weather icon: ${code} (${size}x${size})`);
            const data = await svgToBitmap(svgPath, size, size, true); // Use dithering for weather icons
            const binPath = path.join(DATA_ICONS_DIR, `${code}_${size}.bin`);
            fs.writeFileSync(binPath, data);
            converted++;
        }
    }

    console.log(`\nDone! Converted ${converted} weather icon files, skipped ${skipped} missing SVGs.`);
    console.log(`UI icons header: ${headerPath}`);
    console.log(`Weather icon bins: ${DATA_ICONS_DIR}`);
}

main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});
