/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_AMC_CONSTANTS_H__
#define __GST_AMC_CONSTANTS_H__

#define GST_AMC_SURFACE "amc-surface"
#define GST_AMC_SURFACE_POINTER "amc-surface-pointer"
#define GST_QUARK(x) g_quark_from_string(x)
#define GST_VIDEO_FORMAT_AMC_SURFACE 256

/* FIXME: We might need to get these values from Java if there's
 * ever a device or Android version that changes these values
 */

/* Copies from MediaCodec.java */

enum
{
  BUFFER_FLAG_SYNC_FRAME = 1,
  BUFFER_FLAG_CODEC_CONFIG = 2,
  BUFFER_FLAG_END_OF_STREAM = 4
};

enum
{
  CONFIGURE_FLAG_ENCODE = 1
};

enum
{
  INFO_TRY_AGAIN_LATER = -1,
  INFO_OUTPUT_FORMAT_CHANGED = -2,
  INFO_OUTPUT_BUFFERS_CHANGED = -3
};

/* Copies from MediaCodecInfo.java */
enum
{
  COLOR_FormatMonochrome = 1,
  COLOR_Format8bitRGB332 = 2,
  COLOR_Format12bitRGB444 = 3,
  COLOR_Format16bitARGB4444 = 4,
  COLOR_Format16bitARGB1555 = 5,
  COLOR_Format16bitRGB565 = 6,
  COLOR_Format16bitBGR565 = 7,
  COLOR_Format18bitRGB666 = 8,
  COLOR_Format18bitARGB1665 = 9,
  COLOR_Format19bitARGB1666 = 10,
  COLOR_Format24bitRGB888 = 11,
  COLOR_Format24bitBGR888 = 12,
  COLOR_Format24bitARGB1887 = 13,
  COLOR_Format25bitARGB1888 = 14,
  COLOR_Format32bitBGRA8888 = 15,
  COLOR_Format32bitARGB8888 = 16,
  HAL_PIXEL_FORMAT_YCrCb_420_SP = 0x11, /* = 17 in dec. */
  /* COLOR_FormatYUV411Planar collides with HAL_PIXEL_FORMAT_YCrCb_420_SP.
     We don't use COLOR_FormatYUV411Planar currently, so for now we just remove it. */
/*  COLOR_FormatYUV411Planar = 17, */
  COLOR_FormatYUV411PackedPlanar = 18,
  COLOR_FormatYUV420Planar = 19,
  COLOR_FormatYUV420PackedPlanar = 20,
  COLOR_FormatYUV420SemiPlanar = 21,
  COLOR_FormatYUV422Planar = 22,
  COLOR_FormatYUV422PackedPlanar = 23,
  COLOR_FormatYUV422SemiPlanar = 24,
  COLOR_FormatYCbYCr = 25,
  COLOR_FormatYCrYCb = 26,
  COLOR_FormatCbYCrY = 27,
  COLOR_FormatCrYCbY = 28,
  COLOR_FormatYUV444Interleaved = 29,
  COLOR_FormatRawBayer8bit = 30,
  COLOR_FormatRawBayer10bit = 31,
  COLOR_FormatRawBayer8bitcompressed = 32,
  COLOR_FormatL2 = 33,
  COLOR_FormatL4 = 34,

  /* Another collision: surface of omxrealtekvideodecoder.
     COLOR_FormatL8 is never used in the code. */
  /*  COLOR_FormatL8 = 35, */
  COLOR_FormatSurface6 = 0x23,

  COLOR_FormatL16 = 36,
  COLOR_FormatL24 = 37,
  COLOR_FormatL32 = 38,
  COLOR_FormatYUV420PackedSemiPlanar = 39,
  COLOR_FormatYUV422PackedSemiPlanar = 40,
  COLOR_Format18BitBGR666 = 41,
  COLOR_Format24BitARGB6666 = 42,
  COLOR_Format24BitABGR6666 = 43,
  COLOR_FormatAndroidOpaque = 0x7F000789,
  /* This format is not listed on MediaCodecInfo.CodecCapabilities page */
  COLOR_FormatAndroidUndocumented1 = 47,
  COLOR_TI_FormatYUV420PackedSemiPlanar = 0x7f000100,
  COLOR_QCOM_FormatYUV420SemiPlanar = 0x7fa30c00,
  COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka = 0x7fa30c03,
  /* NV12 but with stride and plane heights aligned to 32 */
  COLOR_QCOM_FormatYVU420SemiPlanar32m = 0x7fa30c04,
  /* From hardware/ti/omap4xxx/domx/omx_core/inc/OMX_TI_IVCommon.h */
  COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced = 0x7f000001,
  COLOR_FormatYUV420Flexible = 0x7f420888,
  COLOR_FormatSurface1 = 256,
  COLOR_FormatSurface2 = 257,
  COLOR_FormatSurface3 = 258,
  COLOR_FormatSurface4 = 259,
  COLOR_FormatSurface5 = 0x32315659,     /* omxrealtekvideodecoder */
  COLOR_FormatSurface7 = 0x00000325     /* huawei */
};

enum
{
  AVCProfileBaseline = 0x01,
  AVCProfileMain = 0x02,
  AVCProfileExtended = 0x04,
  AVCProfileHigh = 0x08,
  AVCProfileHigh10 = 0x10,
  AVCProfileHigh422 = 0x20,
  AVCProfileHigh444 = 0x40
};

enum
{
  AVCLevel1 = 0x01,
  AVCLevel1b = 0x02,
  AVCLevel11 = 0x04,
  AVCLevel12 = 0x08,
  AVCLevel13 = 0x10,
  AVCLevel2 = 0x20,
  AVCLevel21 = 0x40,
  AVCLevel22 = 0x80,
  AVCLevel3 = 0x100,
  AVCLevel31 = 0x200,
  AVCLevel32 = 0x400,
  AVCLevel4 = 0x800,
  AVCLevel41 = 0x1000,
  AVCLevel42 = 0x2000,
  AVCLevel5 = 0x4000,
  AVCLevel51 = 0x8000
};

enum
{
  HEVCProfileMain        = 0x01,
  HEVCProfileMain10      = 0x02,
  HEVCProfileMain10HDR10 = 0x1000
};

enum
{
  HEVCMainTierLevel1  = 0x1,
  HEVCHighTierLevel1  = 0x2,
  HEVCMainTierLevel2  = 0x4,
  HEVCHighTierLevel2  = 0x8,
  HEVCMainTierLevel21 = 0x10,
  HEVCHighTierLevel21 = 0x20,
  HEVCMainTierLevel3  = 0x40,
  HEVCHighTierLevel3  = 0x80,
  HEVCMainTierLevel31 = 0x100,
  HEVCHighTierLevel31 = 0x200,
  HEVCMainTierLevel4  = 0x400,
  HEVCHighTierLevel4  = 0x800,
  HEVCMainTierLevel41 = 0x1000,
  HEVCHighTierLevel41 = 0x2000,
  HEVCMainTierLevel5  = 0x4000,
  HEVCHighTierLevel5  = 0x8000,
  HEVCMainTierLevel51 = 0x10000,
  HEVCHighTierLevel51 = 0x20000,
  HEVCMainTierLevel52 = 0x40000,
  HEVCHighTierLevel52 = 0x80000,
  HEVCMainTierLevel6  = 0x100000,
  HEVCHighTierLevel6  = 0x200000,
  HEVCMainTierLevel61 = 0x400000,
  HEVCHighTierLevel61 = 0x800000,
  HEVCMainTierLevel62 = 0x1000000,
  HEVCHighTierLevel62 = 0x2000000
};

static const int HEVCHighTierLevels =
  HEVCHighTierLevel1 | HEVCHighTierLevel2 | HEVCHighTierLevel21 | HEVCHighTierLevel3 |
  HEVCHighTierLevel31 | HEVCHighTierLevel4 | HEVCHighTierLevel41 | HEVCHighTierLevel5 |
  HEVCHighTierLevel51 | HEVCHighTierLevel52 | HEVCHighTierLevel6 | HEVCHighTierLevel61 |
  HEVCHighTierLevel62;
  
enum
{
  H263ProfileBaseline = 0x01,
  H263ProfileH320Coding = 0x02,
  H263ProfileBackwardCompatible = 0x04,
  H263ProfileISWV2 = 0x08,
  H263ProfileISWV3 = 0x10,
  H263ProfileHighCompression = 0x20,
  H263ProfileInternet = 0x40,
  H263ProfileInterlace = 0x80,
  H263ProfileHighLatency = 0x100
};

enum
{
  H263Level10 = 0x01,
  H263Level20 = 0x02,
  H263Level30 = 0x04,
  H263Level40 = 0x08,
  H263Level45 = 0x10,
  H263Level50 = 0x20,
  H263Level60 = 0x40,
  H263Level70 = 0x80
};

enum
{
  MPEG4ProfileSimple = 0x01,
  MPEG4ProfileSimpleScalable = 0x02,
  MPEG4ProfileCore = 0x04,
  MPEG4ProfileMain = 0x08,
  MPEG4ProfileNbit = 0x10,
  MPEG4ProfileScalableTexture = 0x20,
  MPEG4ProfileSimpleFace = 0x40,
  MPEG4ProfileSimpleFBA = 0x80,
  MPEG4ProfileBasicAnimated = 0x100,
  MPEG4ProfileHybrid = 0x200,
  MPEG4ProfileAdvancedRealTime = 0x400,
  MPEG4ProfileCoreScalable = 0x800,
  MPEG4ProfileAdvancedCoding = 0x1000,
  MPEG4ProfileAdvancedCore = 0x2000,
  MPEG4ProfileAdvancedScalable = 0x4000,
  MPEG4ProfileAdvancedSimple = 0x8000
};

enum
{
  MPEG4Level0 = 0x01,
  MPEG4Level0b = 0x02,
  MPEG4Level1 = 0x04,
  MPEG4Level2 = 0x08,
  MPEG4Level3 = 0x10,
  MPEG4Level4 = 0x20,
  MPEG4Level4a = 0x40,
  MPEG4Level5 = 0x80
};

enum
{
  AACObjectMain = 1,
  AACObjectLC = 2,
  AACObjectSSR = 3,
  AACObjectLTP = 4,
  AACObjectHE = 5,
  AACObjectScalable = 6,
  AACObjectERLC = 17,
  AACObjectLD = 23,
  AACObjectHE_PS = 29,
  AACObjectELD = 39
};

/* Copies from AudioFormat.java */
enum
{
  CHANNEL_OUT_FRONT_LEFT = 0x4,
  CHANNEL_OUT_FRONT_RIGHT = 0x8,
  CHANNEL_OUT_FRONT_CENTER = 0x10,
  CHANNEL_OUT_LOW_FREQUENCY = 0x20,
  CHANNEL_OUT_BACK_LEFT = 0x40,
  CHANNEL_OUT_BACK_RIGHT = 0x80,
  CHANNEL_OUT_FRONT_LEFT_OF_CENTER = 0x100,
  CHANNEL_OUT_FRONT_RIGHT_OF_CENTER = 0x200,
  CHANNEL_OUT_BACK_CENTER = 0x400,
  CHANNEL_OUT_SIDE_LEFT = 0x800,
  CHANNEL_OUT_SIDE_RIGHT = 0x1000,
  CHANNEL_OUT_TOP_CENTER = 0x2000,
  CHANNEL_OUT_TOP_FRONT_LEFT = 0x4000,
  CHANNEL_OUT_TOP_FRONT_CENTER = 0x8000,
  CHANNEL_OUT_TOP_FRONT_RIGHT = 0x10000,
  CHANNEL_OUT_TOP_BACK_LEFT = 0x20000,
  CHANNEL_OUT_TOP_BACK_CENTER = 0x40000,
  CHANNEL_OUT_TOP_BACK_RIGHT = 0x80000
};

/* From MediaCodecInfo.CodecCapabilities*/
enum
{
  FEATURE_ADAPTIVE_PLAYBACK = 0,
  FEATURE_DYNAMIC_TIMESTAMP,
  FEATURE_FRAME_PARSING,
  FEATURE_INTRA_REFRESH,
  FEATURE_LOW_LATENCY,
  FEATURE_MULTIPLE_FRAMES,
  FEATURE_PARTIAL_FRAME,
  FEATURE_SECURE_PLAYBACK,
  FEATURE_TUNNELED_PLAYBACK,
  FEATURE_COUNT
};

#endif
