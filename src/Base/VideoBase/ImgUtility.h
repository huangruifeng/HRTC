/*
 * @Author: huangruifeng
 * @Date: 2021-07-01 14:29:46
 * @LastEditTime: 2021-07-01 15:15:06
 * @LastEditors: huangruifeng
 * @FilePath: \Base\VideoBase\ImgUtility.h
 * @Description: 
 */

#ifndef _IMG_UTILITY_H__
#define _IMG_UTILITY_H__
#include <cstdint>

#define CLIP(X) ( (X) > 255 ? 255 : (X) < 0 ? 0 : X)

// RGB -> YUV
#define RGB2Y(R, G, B) CLIP(( (  66 * (R) + 129 * (G) +  25 * (B) + 128) >> 8) +  16)
#define RGB2U(R, G, B) CLIP(( ( -38 * (R) -  74 * (G) + 112 * (B) + 128) >> 8) + 128)
#define RGB2V(R, G, B) CLIP(( ( 112 * (R) -  94 * (G) -  18 * (B) + 128) >> 8) + 128)

// YUV -> RGB
#define C(Y) ( (Y) - 16  )
#define D(U) ( (U) - 128 )
#define E(V) ( (V) - 128 )

#define YUV2R(Y, U, V) CLIP(( 298 * C(Y)              + 409 * E(V) + 128) >> 8)
#define YUV2G(Y, U, V) CLIP(( 298 * C(Y) - 100 * D(U) - 208 * E(V) + 128) >> 8)
#define YUV2B(Y, U, V) CLIP(( 298 * C(Y) + 516 * D(U)              + 128) >> 8)

// RGB -> YCbCr
#define CRGB2Y(R, G, B) CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16)
#define CRGB2Cb(R, G, B) CLIP((36962 * (B - CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16) ) >> 16) + 128)
#define CRGB2Cr(R, G, B) CLIP((46727 * (R - CLIP((19595 * R + 38470 * G + 7471 * B ) >> 16) ) >> 16) + 128)

// YCbCr -> RGB
#define CYCbCr2R(Y, Cb, Cr) CLIP( Y + ( 91881 * Cr >> 16 ) - 179 )
#define CYCbCr2G(Y, Cb, Cr) CLIP( Y - (( 22544 * Cb + 46793 * Cr ) >> 16) + 135)
#define CYCbCr2B(Y, Cb, Cr) CLIP( Y + (116129 * Cb >> 16 ) - 226 )

typedef struct YUVImg {
    uint8_t *yPtr;
    uint8_t *uPtr;
    uint8_t *vPtr;
    int yStride;
    int uStride;
    int vStride;
    int inputWidth;
    int inputHeight;
} YUVImg;

typedef struct RGBImg {
    uint8_t *data;
    int width;
    int height;
    int comp;
} RGBImg;

void mergeRGBToY420(YUVImg* yuv,RGBImg* rgb ,int xPos, int yPos,float opacity = 0) {
    for (int i = 0; i < rgb->height; i++) {
        const uint8_t* txtImgRow = rgb->data + i * rgb->comp * rgb->width;
        if (yPos + i >= yuv->inputHeight)
            continue;
        uint8_t* yRow = yuv->yPtr + (yPos + i) * yuv->yStride;
        uint8_t* uRow = yuv->uPtr + ((yPos + i) / 2) * yuv->uStride;
        uint8_t* vRow = yuv->vPtr + ((yPos + i) / 2) * yuv->vStride;

        for (int j = 0; j < rgb->width; j++) {
            if (xPos + j >= yuv->inputWidth)
                continue;

            int step = j << 2;
            uint8_t alpha = txtImgRow[step + 3];
            if (alpha == 0)
                continue;

            uint8_t red = txtImgRow[step];
            uint8_t green = txtImgRow[step + 1];
            uint8_t blue = txtImgRow[step + 2];

            int uvPos = (xPos + j) >> 1;
            float a = alpha * opacity / 255 ;
            float b = 1 - a;

            yRow[xPos + j] = RGB2Y(red, green, blue) * a + yRow[xPos + j] * b;
            if (j % 2 == 0 && i % 2 == 0)
            {
                uRow[uvPos] = RGB2U(red, green, blue) * a + uRow[uvPos] * b;
                vRow[uvPos] = RGB2V(red, green, blue) * a + vRow[uvPos] * b;
            }
        }
    }
}

#endif /* _IMG_UTILITY_H__ */