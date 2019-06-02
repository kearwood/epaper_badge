/*
 * Animated GIFs Display Code for SmartMatrix and 32x32 RGB LED Panels
 *
 * This file contains code to parse animated GIF files
 *
 * Written by: Craig A. Lindley
 *
 * Copyright (c) 2014 Craig A. Lindley
 * Minor modifications by Louis Beaudoin (pixelmatix)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define GIFDEBUG 0

#if defined (ARDUINO)
#include <Arduino.h>
#elif defined (SPARK)
#include "application.h"
#endif

// This file contains C code, and ESP32 Arduino has changed to use the C++ template version of min()/max() which we can't use with C, so we can't depend on a #define min() from Arduino anymore
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

#include "GifDecoder.h"

#if GIFDEBUG == 1
#define DEBUG_SCREEN_DESCRIPTOR                             1
#define DEBUG_GLOBAL_COLOR_TABLE                            1
#define DEBUG_PROCESSING_PLAIN_TEXT_EXT                     1
#define DEBUG_PROCESSING_GRAPHIC_CONTROL_EXT                1
#define DEBUG_PROCESSING_APP_EXT                            1
#define DEBUG_PROCESSING_COMMENT_EXT                        1
#define DEBUG_PROCESSING_FILE_TERM                          1
#define DEBUG_PROCESSING_TABLE_IMAGE_DESC                   1
#define DEBUG_PROCESSING_TBI_DESC_START                     1
#define DEBUG_PROCESSING_TBI_DESC_INTERLACED                1
#define DEBUG_PROCESSING_TBI_DESC_LOCAL_COLOR_TABLE         1
#define DEBUG_PROCESSING_TBI_DESC_LZWCODESIZE               1
#define DEBUG_PROCESSING_TBI_DESC_DATABLOCKSIZE             1
#define DEBUG_PROCESSING_TBI_DESC_LZWIMAGEDATA_OVERFLOW     1
#define DEBUG_PROCESSING_TBI_DESC_LZWIMAGEDATA_SIZE         1
#define DEBUG_PARSING_DATA                                  1
#define DEBUG_DECOMPRESS_AND_DISPLAY                        1

#define DEBUG_WAIT_FOR_KEY_PRESS                            0

#endif

// Error codes
#define ERROR_NONE                 0
#define ERROR_DONE_PARSING         1
#define ERROR_WAITING              2
#define ERROR_FILEOPEN             -1
#define ERROR_FILENOTGIF           -2
#define ERROR_BADGIFFORMAT         -3
#define ERROR_UNKNOWNCONTROLEXT    -4

#define GIFHDRTAGNORM   "GIF87a"  // tag in valid GIF file
#define GIFHDRTAGNORM1  "GIF89a"  // tag in valid GIF file
#define GIFHDRSIZE 6

// Global GIF specific definitions
#define COLORTBLFLAG    0x80
#define INTERLACEFLAG   0x40
#define TRANSPARENTFLAG 0x01

#define NO_TRANSPARENT_INDEX -1


template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::setStartDrawingCallback(callback f) {
    startDrawingCallback = f;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::setUpdateScreenCallback(callback f) {
    updateScreenCallback = f;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::setDrawPixelCallback(pixel_callback f) {
    drawPixelCallback = f;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::setScreenClearCallback(callback f) {
    screenClearCallback = f;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::setFileSeekCallback(file_seek_callback f) {
    fileSeekCallback = f;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::setFilePositionCallback(file_position_callback f) {
    filePositionCallback = f;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::setFileReadCallback(file_read_callback f) {
    fileReadCallback = f;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::setFileReadBlockCallback(file_read_block_callback f) {
    fileReadBlockCallback = f;
}

// Backup the read stream by n bytes
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::backUpStream(int n) {
    fileSeekCallback(filePositionCallback() - n);
}

// Read a file byte
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
int GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::readByte() {

    int b = fileReadCallback();
    printf("GifDecoder::readByte got %i\r\n", b);
    if (b == -1) {
#if GIFDEBUG == 1
        Serial.println("Read error or EOF occurred");
#endif
    }
    return b;
}

// Read a file word
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
int GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::readWord() {

    int b0 = readByte();
    int b1 = readByte();
    return (b1 << 8) | b0;
}

// Read the specified number of bytes into the specified buffer
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
int GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::readIntoBuffer(void *buffer, int numberOfBytes) {

    int result = fileReadBlockCallback(buffer, numberOfBytes);
    if (result == -1) {
        printf("Read error or EOF occurred\r\n");
    }
    return result;
}

// Make sure the file is a Gif file
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
bool GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseGifHeader() {

    char buffer[10];

    readIntoBuffer(buffer, GIFHDRSIZE);
    if ((strncmp(buffer, GIFHDRTAGNORM,  GIFHDRSIZE) != 0) &&
        (strncmp(buffer, GIFHDRTAGNORM1, GIFHDRSIZE) != 0))  {
        return false;
    }
    else    {
        return true;
    }
}

// Parse the logical screen descriptor
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseLogicalScreenDescriptor() {

    lsdWidth = readWord();
    lsdHeight = readWord();
    lsdPackedField = readByte();
    lsdBackgroundIndex = readByte();
    lsdAspectRatio = readByte();
}

// Parse the global color table
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseGlobalColorTable() {

    // Does a global color table exist?
    if (lsdPackedField & COLORTBLFLAG) {

        // A GCT was present determine how many colors it contains
        colorCount = 1 << ((lsdPackedField & 7) + 1);

#if GIFDEBUG == 1 && DEBUG_GLOBAL_COLOR_TABLE == 1
        Serial.print("Global color table with ");
        Serial.print(colorCount);
        Serial.println(" colors present");
#endif
        // Read color values into the palette array
        int colorTableBytes = sizeof(rgb_24) * colorCount;
        readIntoBuffer(palette, colorTableBytes);
    }
}

// Parse plain text extension and dispose of it
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parsePlainTextExtension() {

#if GIFDEBUG == 1 && DEBUG_PROCESSING_PLAIN_TEXT_EXT == 1
    Serial.println("\nProcessing Plain Text Extension");
#endif
    // Read plain text header length
    uint8_t len = readByte();

    // Consume plain text header data
    readIntoBuffer(tempBuffer, len);

    // Consume the plain text data in blocks
    len = readByte();
    while (len != 0) {
        readIntoBuffer(tempBuffer, len);
        len = readByte();
    }
}

// Parse a graphic control extension
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseGraphicControlExtension() {

    int len = readByte();   // Check length
    if (len != 4) {
        printf("Bad graphic control extension\r\n");
    }

    int packedBits = readByte();
    frameDelay = readWord();
    transparentColorIndex = readByte();

    if ((packedBits & TRANSPARENTFLAG) == 0) {
        // Indicate no transparent index
        transparentColorIndex = NO_TRANSPARENT_INDEX;
    }

    readByte(); // Toss block end

}

// Parse application extension
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseApplicationExtension() {

    memset(tempBuffer, 0, sizeof(tempBuffer));

    // Read block length
    uint8_t len = readByte();

    // Read app data
    readIntoBuffer(tempBuffer, len);

    // Consume any additional app data
    len = readByte();
    while (len != 0) {
        readIntoBuffer(tempBuffer, len);
        len = readByte();
    }
}

// Parse comment extension
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseCommentExtension() {

    // Read block length
    uint8_t len = readByte();
    while (len != 0) {
        // Clear buffer
        memset(tempBuffer, 0, sizeof(tempBuffer));

        // Read len bytes into buffer
        readIntoBuffer(tempBuffer, len);

        // Read the new block length
        len = readByte();
    }
}

// Parse file terminator
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
int GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseGIFFileTerminator() {
    uint8_t b = readByte();
    if (b != 0x3B) {
        printf("Bad GIF file format - Bad terminator\r\n");
        return ERROR_BADGIFFORMAT;
    }
    else    {
        return ERROR_NONE;
    }
}

// Parse table based image data
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseTableBasedImage() {

    // Parse image descriptor
    tbiImageX = readWord();
    tbiImageY = readWord();
    tbiWidth = readWord();
    tbiHeight = readWord();
    tbiPackedBits = readByte();

    // Is this image interlaced ?
    tbiInterlaced = ((tbiPackedBits & INTERLACEFLAG) != 0);

    // Does this image have a local color table ?
    bool localColorTable =  ((tbiPackedBits & COLORTBLFLAG) != 0);

    if (localColorTable) {
        int colorBits = ((tbiPackedBits & 7) + 1);
        colorCount = 1 << colorBits;

        // Read colors into palette
        int colorTableBytes = sizeof(rgb_24) * colorCount;
        readIntoBuffer(palette, colorTableBytes);
    }

    // One time initialization of imageData before first frame
    if (keyFrame) {
        /*
        if (transparentColorIndex == NO_TRANSPARENT_INDEX) {
            fillImageData(lsdBackgroundIndex);
        }
        else    {
            fillImageData(transparentColorIndex);
        }
        */
        keyFrame = false;
    }

    // Read the min LZW code size
    lzwCodeSize = readByte();

    unsigned long filePositionBefore = filePositionCallback();

    // Gather the lzw image data
    // NOTE: the dataBlockSize byte is left in the data as the lzw decoder needs it
    int offset = 0;
    int dataBlockSize = readByte();
    while (dataBlockSize != 0) {
        backUpStream(1);
        dataBlockSize++;
        fileSeekCallback(filePositionCallback() + dataBlockSize);

        offset += dataBlockSize;
        dataBlockSize = readByte();
    }

    // this is the position where GIF decoding needs to pick up after decompressing frame
    unsigned long filePositionAfter = filePositionCallback();

    fileSeekCallback(filePositionBefore);

    // Process the animation frame for display

    // Initialize the LZW decoder for this frame
    lzw_decode_init(lzwCodeSize);
    lzw_setTempBuffer((uint8_t*)tempBuffer);

    // Make sure there is at least some delay between frames
    if (frameDelay < 1) {
        frameDelay = 1;
    }

    // Decompress LZW data and display the frame
    decompressAndDisplayFrame(filePositionAfter);

    // Graphic control extension is for a single frame
    transparentColorIndex = NO_TRANSPARENT_INDEX;
}

// Parse gif data
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
int GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::parseData() {
    printf("parseData\r\n");
    bool parsedFrame = false;
    while (!parsedFrame) {


        // Determine what kind of data to process
        uint8_t b = readByte();
        printf("parseData - readByte returns %i\r\n", b);

        if (b == 0x2c) {
            // Parse table based image
            parseTableBasedImage();
            parsedFrame = true;

        }
        else if (b == 0x21) {
            // Parse extension
            b = readByte();

            // Determine which kind of extension to parse
            switch (b) {
            case 0x01:
                // Plain test extension
                parsePlainTextExtension();
                break;
            case 0xf9:
                // Graphic control extension
                parseGraphicControlExtension();
                break;
            case 0xfe:
                // Comment extension
                parseCommentExtension();
                break;
            case 0xff:
                // Application extension
                parseApplicationExtension();
                break;
            default:
                printf("Unknown control extension\r\n");
                //printf(b, HEX);
                return ERROR_UNKNOWNCONTROLEXT;
            }
        }
        else    {
#if GIFDEBUG == 1 && DEBUG_PARSING_DATA == 1
    Serial.println("\nParsing Done");
#endif

            // Push unprocessed byte back into the stream for later processing
            backUpStream(1);
            printf("parseData - ERROR_DONE_PARSING\r\n");
            return ERROR_DONE_PARSING;
        }
    }
    printf("parseData - ERROR_NONE\r\n");
    return ERROR_NONE;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
int GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::startDecoding(void) {
    // Initialize variables
    keyFrame = true;
    transparentColorIndex = NO_TRANSPARENT_INDEX;
    nextFrameTime_ms = 0;
    fileSeekCallback(0);

    // Validate the header
    if (! parseGifHeader()) {
        printf("Not a GIF file\r\n");
        return ERROR_FILENOTGIF;
    }
    // If we get here we have a gif file to process

    // Parse the logical screen descriptor
    parseLogicalScreenDescriptor();

    // Parse the global color table
    parseGlobalColorTable();

    return ERROR_NONE;
}

template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
int GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::decodeFrame(void) {
    // Parse gif data
    int result = parseData();
    if (result < ERROR_NONE) {
        printf("Error: %i occurred during parsing of data\r\n", result);
        return result;
    }

    if (result == ERROR_DONE_PARSING) {
        //startDecoding();
        // Initialize variables like with a new file
        keyFrame = true;
        transparentColorIndex = NO_TRANSPARENT_INDEX;
        nextFrameTime_ms = 0;
        fileSeekCallback(0);

        // parse Gif Header like with a new file
        parseGifHeader();

        // Parse the logical screen descriptor
        parseLogicalScreenDescriptor();

        // Parse the global color table
        parseGlobalColorTable();
    }

    return result;
}

// Decompress LZW data and display animation frame
template <int maxGifWidth, int maxGifHeight, int lzwMaxBits>
void GifDecoder<maxGifWidth, maxGifHeight, lzwMaxBits>::decompressAndDisplayFrame(unsigned long filePositionAfter) {


    printf("decompressAndDisplayFrame\r\n");
    // Each pixel of image is 8 bits and is an index into the palette

        // How the image is decoded depends upon whether it is interlaced or not
    // Decode the interlaced LZW data into the image buffer
    if (tbiInterlaced) {
        // Decode every 8th line starting at line 0
        for (int line = tbiImageY + 0; line < tbiHeight + tbiImageY; line += 8) {
            lzw_decode(rowDecodeBuffer + tbiImageX, tbiWidth, rowDecodeBuffer + maxGifWidth);
            for (int x = tbiImageX; x < tbiWidth + tbiImageX; x++) {
                // Get the next pixel
                int pixel = rowDecodeBuffer[x];

                // Check pixel transparency
                if (pixel == transparentColorIndex) {
                    continue;
                }

                // Pixel not transparent so get color from palette and draw the pixel
                if(drawPixelCallback)
                    (*drawPixelCallback)(x, line, palette[pixel].red, palette[pixel].green, palette[pixel].blue);
            }
        }
        // Decode every 8th line starting at line 4
        for (int line = tbiImageY + 4; line < tbiHeight + tbiImageY; line += 8) {
            lzw_decode(rowDecodeBuffer + tbiImageX, tbiWidth, rowDecodeBuffer + maxGifWidth);
            for (int x = tbiImageX; x < tbiWidth + tbiImageX; x++) {
                // Get the next pixel
                int pixel = rowDecodeBuffer[x];

                // Check pixel transparency
                if (pixel == transparentColorIndex) {
                    continue;
                }

                // Pixel not transparent so get color from palette and draw the pixel
                if(drawPixelCallback)
                    (*drawPixelCallback)(x, line, palette[pixel].red, palette[pixel].green, palette[pixel].blue);
            }
        }
        // Decode every 4th line starting at line 2
        for (int line = tbiImageY + 2; line < tbiHeight + tbiImageY; line += 4) {
            lzw_decode(rowDecodeBuffer + tbiImageX, tbiWidth, rowDecodeBuffer + maxGifWidth);
            for (int x = tbiImageX; x < tbiWidth + tbiImageX; x++) {
                // Get the next pixel
                int pixel = rowDecodeBuffer[x];

                // Check pixel transparency
                if (pixel == transparentColorIndex) {
                    continue;
                }

                // Pixel not transparent so get color from palette and draw the pixel
                if(drawPixelCallback)
                    (*drawPixelCallback)(x, line, palette[pixel].red, palette[pixel].green, palette[pixel].blue);
            }
        }
        // Decode every 2nd line starting at line 1
        for (int line = tbiImageY + 1; line < tbiHeight + tbiImageY; line += 2) {
            lzw_decode(rowDecodeBuffer + tbiImageX, tbiWidth, rowDecodeBuffer + maxGifWidth);
            for (int x = tbiImageX; x < tbiWidth + tbiImageX; x++) {
                // Get the next pixel
                int pixel = rowDecodeBuffer[x];

                // Check pixel transparency
                if (pixel == transparentColorIndex) {
                    continue;
                }

                // Pixel not transparent so get color from palette and draw the pixel
                if(drawPixelCallback)
                    (*drawPixelCallback)(x, line, palette[pixel].red, palette[pixel].green, palette[pixel].blue);
            }
        }
    }
    else    {
        // Decode the non interlaced LZW data into the image data buffer
        for (int line = tbiImageY; line < tbiHeight + tbiImageY; line++) {
            lzw_decode(rowDecodeBuffer + tbiImageX, tbiWidth, rowDecodeBuffer + maxGifWidth);
            for (int x = tbiImageX; x < tbiWidth + tbiImageX; x++) {
                // Get the next pixel
                int pixel = rowDecodeBuffer[x];

                // Check pixel transparency
                if (pixel == transparentColorIndex) {
                    continue;
                }

                // Pixel not transparent so get color from palette and draw the pixel
                if(drawPixelCallback)
                    (*drawPixelCallback)(x, line, palette[pixel].red, palette[pixel].green, palette[pixel].blue);
            }
        }
    }

    // LZW doesn't parse through all the data, manually set position
    fileSeekCallback(filePositionAfter);
}
