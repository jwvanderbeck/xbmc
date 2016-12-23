/*
 *      Copyright (C) 2010-2015 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#if (defined HAVE_CONFIG_H)
  #include "config.h"
#endif
#define HAVE_VIDEOTOOLBOXDECODER
#if defined(HAVE_VIDEOTOOLBOXDECODER)
#include "DynamicDll.h"
#include "DVDClock.h"
#include "DVDCodecUtils.h"
#include "DVDVideoCodecVideoToolBox.h"
#include "settings/Settings.h"
#include "settings/AdvancedSettings.h"
#include "utils/BitstreamConverter.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "platform/darwin/DarwinUtils.h"
#include "platform/darwin/DictionaryUtils.h"

extern "C" {
#include "libavformat/avformat.h"
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
#if defined(__cplusplus)
extern "C"
{
#endif
#pragma pack(push, 4)
// /System/Library/PrivateFrameworks/VideoToolbox.framework
enum VTFormat {
  kVTFormatJPEG         = 'jpeg', // kCMVideoCodecType_JPEG
  kVTFormatH264         = 'avc1', // kCMVideoCodecType_H264 (MPEG-4 Part 10)
  kVTFormatH265         = 'hvc1', // kCMVideoCodecType_H265 (MPEG-4 Part 15)
  kVTFormatMPEG4Video   = 'mp4v', // kCMVideoCodecType_MPEG4Video (MPEG-4 Part 2)
  kVTFormatMPEG2Video   = 'mp2v'  // kCMVideoCodecType_MPEG2Video
};
enum {
  kVTDecoderNoErr = 0,
  kVTDecoderHardwareNotSupportedErr = -12470,
  kVTDecoderFormatNotSupportedErr = -12471,
  kVTDecoderConfigurationError = -12472,
  kVTDecoderDecoderFailedErr = -12473,
  kVTVideoDecoderMalfunctionErr = -12911,
  kVTInvalidSessionErr = -12903,
};
enum {
  kVTDecodeInfo_Asynchronous = 1UL << 0,
  kVTDecodeInfo_FrameDropped = 1UL << 1,
  kVTDecodeInfo_1xRealTimePlayback = 1<<2,
  kVTDecodeInfo_EnableTemporalProcessing = 1<<3
};
enum {
  // tells the decoder not to bother returning a CVPixelBuffer
  // in the outputCallback. The output callback will still be called.
  kVTDecoderDecodeFlags_DontEmitFrame = 1 << 1,
};
enum {
  // decode and return buffers for all frames currently in flight.
  kVTDecoderFlush_EmitFrames = 1 << 0          
};

typedef UInt32 VTFormatId;
typedef CFTypeRef VTDecompressionSessionRef;

typedef void (*VTDecompressionOutputCallbackFunc)(
  void            *refCon,
  CFDictionaryRef frameInfo,
  OSStatus        status,
  UInt32          infoFlags,
  CVBufferRef     imageBuffer,
  CMTime          pts,
  CMTime          duration);

typedef struct _VTDecompressionOutputCallback VTDecompressionOutputCallback;
struct _VTDecompressionOutputCallback {
  VTDecompressionOutputCallbackFunc callback;
  void *refcon;
};

OSStatus VTDecompressionSessionCreate(
  CFAllocatorRef allocator,
  CMFormatDescriptionRef videoFormatDescription,
  CFTypeRef sessionOptions,
  CFDictionaryRef destinationPixelBufferAttributes,
  VTDecompressionOutputCallback *outputCallback,
  VTDecompressionSessionRef *session);

OSStatus VTDecompressionSessionDecodeFrame(
  VTDecompressionSessionRef session, CMSampleBufferRef sbuf,
  uint32_t decoderFlags, CFDictionaryRef frameInfo, uint32_t unk1);

OSStatus VTDecompressionSessionCopyProperty(VTDecompressionSessionRef session, CFTypeRef key, void* unk, CFTypeRef * value);
OSStatus VTDecompressionSessionCopySupportedPropertyDictionary(VTDecompressionSessionRef session, CFDictionaryRef * dict);
OSStatus VTDecompressionSessionSetProperty(VTDecompressionSessionRef session, CFStringRef propName, CFTypeRef propValue);
void VTDecompressionSessionInvalidate(VTDecompressionSessionRef session);
OSStatus VTDecompressionSessionWaitForAsynchronousFrames(VTDecompressionSessionRef session);
#pragma pack(pop)
    
#if defined(__cplusplus)
}
#endif

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
class DllVideoToolBoxInterface
{
public:
  virtual ~DllVideoToolBoxInterface() {}

  virtual OSStatus VTDecompressionSessionCreate(CFAllocatorRef allocator, CMFormatDescriptionRef videoFormatDescription, CFTypeRef sessionOptions, CFDictionaryRef destinationPixelBufferAttributes, VTDecompressionOutputCallback *outputCallback, VTDecompressionSessionRef *session) = 0;
  virtual OSStatus VTDecompressionSessionDecodeFrame(VTDecompressionSessionRef session, CMSampleBufferRef sbuf, uint32_t decoderFlags, CFDictionaryRef frameInfo, uint32_t unk1) = 0;
  virtual OSStatus VTDecompressionSessionCopyProperty(VTDecompressionSessionRef session, CFTypeRef key, void* unk, CFTypeRef * value) = 0;
  virtual OSStatus VTDecompressionSessionCopySupportedPropertyDictionary(VTDecompressionSessionRef session, CFDictionaryRef * dict) = 0;
  virtual OSStatus VTDecompressionSessionSetProperty(VTDecompressionSessionRef session, CFStringRef propName, CFTypeRef propValue) = 0;
  virtual void VTDecompressionSessionInvalidate(VTDecompressionSessionRef session) = 0;
  virtual OSStatus VTDecompressionSessionWaitForAsynchronousFrames(VTDecompressionSessionRef session) = 0;
};

class DllVideoToolBox : public DllDynamic, public DllVideoToolBoxInterface
{
  DECLARE_DLL_WRAPPER(DllVideoToolBox, "/System/Library/Frameworks/VideoToolbox.framework/VideoToolbox")
  DEFINE_METHOD6(OSStatus, VTDecompressionSessionCreate, (CFAllocatorRef p1, CMFormatDescriptionRef p2, CFTypeRef p3, CFDictionaryRef p4, VTDecompressionOutputCallback *p5, VTDecompressionSessionRef *p6))
  DEFINE_METHOD5(OSStatus, VTDecompressionSessionDecodeFrame, (VTDecompressionSessionRef p1, CMSampleBufferRef p2, uint32_t p3, CFDictionaryRef p4, uint32_t p5))
  DEFINE_METHOD4(OSStatus, VTDecompressionSessionCopyProperty, (VTDecompressionSessionRef p1, CFTypeRef p2, void* p3, CFTypeRef * p4))
  DEFINE_METHOD2(OSStatus, VTDecompressionSessionCopySupportedPropertyDictionary, (VTDecompressionSessionRef p1, CFDictionaryRef * p2))
  DEFINE_METHOD3(OSStatus, VTDecompressionSessionSetProperty, (VTDecompressionSessionRef p1, CFStringRef p2, CFTypeRef p3))
  DEFINE_METHOD1(void, VTDecompressionSessionInvalidate, (VTDecompressionSessionRef p1))
  DEFINE_METHOD1(OSStatus, VTDecompressionSessionWaitForAsynchronousFrames, (VTDecompressionSessionRef p1))

  BEGIN_METHOD_RESOLVE()
    RESOLVE_METHOD_RENAME(VTDecompressionSessionCreate, VTDecompressionSessionCreate)
    RESOLVE_METHOD_RENAME(VTDecompressionSessionDecodeFrame, VTDecompressionSessionDecodeFrame)
    RESOLVE_METHOD_RENAME(VTDecompressionSessionCopyProperty, VTDecompressionSessionCopyProperty)
    RESOLVE_METHOD_RENAME(VTDecompressionSessionCopySupportedPropertyDictionary, VTDecompressionSessionCopySupportedPropertyDictionary)
    RESOLVE_METHOD_RENAME(VTDecompressionSessionSetProperty, VTDecompressionSessionSetProperty)
    RESOLVE_METHOD_RENAME(VTDecompressionSessionInvalidate, VTDecompressionSessionInvalidate)
    RESOLVE_METHOD_RENAME(VTDecompressionSessionWaitForAsynchronousFrames, VTDecompressionSessionWaitForAsynchronousFrames)
  END_METHOD_RESOLVE()
};

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
static int CheckNP2( unsigned x )
{
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return ++x;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
typedef struct VTDumpDecompressionPropCtx
{
  CDVDVideoCodecVideoToolBox *ctx;
  VTDecompressionSessionRef session;
} VTDumpDecompressionPropCtx;

// helper functions for debuging VTDecompression
static char* vtutil_string_to_utf8(CFStringRef s)
{
  char *result = nullptr;

  CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength (s), kCFStringEncodingUTF8);
  result = (char*)malloc(size + 1);
  CFStringGetCString(s, result, size + 1, kCFStringEncodingUTF8);

  return result;
}

static char* vtutil_object_to_string(CFTypeRef obj)
{
  char *result = nullptr;

  if (obj == NULL)
    return strdup ("(null)");

  CFStringRef s = CFCopyDescription(obj);
  result = vtutil_string_to_utf8(s);
  CFRelease(s);

  return result;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
static void write_mp4_description_length(AVIOContext *context, int length)
{
  for (int i = 3; i >= 0; i--)
  {
    uint8_t byte = (length >> (i * 7)) & 0x7F;
    if (i != 0)
        byte |= 0x80;

    avio_w8(context, byte);
  }
}

static CFDataRef ESDSCreate(const uint8_t *p_buf, int i_buf_size)
{
  int full_size = 3 + 5 + 13 + 5 + i_buf_size + 3;
  int config_size = 13 + 5 + i_buf_size;
  int padding = 12;

  AVIOContext *context;
  int status = avio_open_dyn_buf(&context);
  if (status != noErr)
    CLog::Log(LOGERROR,"VideoToolBox: opening dyn buf failed (%i).", status);

  avio_w8(context, 0);        // Version
  avio_wb24(context, 0);      // Flags

  // elementary stream description tag
  avio_w8(context, 0x03);     // ES description tag
  write_mp4_description_length(context, full_size);
  avio_wb16(context, 0);      // esid
  avio_w8(context, 0);        // stream priority (0-3)

  // decoder configuration description tag
  avio_w8(context, 0x04);
  write_mp4_description_length(context, config_size);
  avio_w8(context, 32);       // object type identification (32 = MPEG4)
  avio_w8(context, 0x11);     // stream type
  avio_wb24(context, 0);      // buffer size
  avio_wb32(context, 0);      // max bitrate
  avio_wb32(context, 0);      // avg bitrate

  // decoder specific description tag
  avio_w8(context, 0x05);     // dec specific info tag
  write_mp4_description_length(context, i_buf_size);
  avio_write(context, p_buf, i_buf_size);

  // sync layer configuration description tag
  avio_w8(context, 0x06);     // tag
  avio_w8(context, 0x01);     // length
  avio_w8(context, 0x02);     // no SL

  uint8_t *rw_extradata = (uint8_t*)malloc(full_size + padding);
  avio_close_dyn_buf(context, &rw_extradata);

  CFDataRef data = CFDataCreate(kCFAllocatorDefault,
    rw_extradata, full_size + padding);

  return data;
}

static CFDataRef avvCCreate(const uint8_t *p_buf, int i_buf_size)
{
  CFDataRef data;
  // each NAL sent to the decoder is preceded by a 4 byte header
  // we need to change the avcC header to signal headers of 4 bytes, if needed
  if (i_buf_size >= 4 && (p_buf[4] & 0x03) != 0x03)
  {
    uint8_t *p_fixed_buf = (uint8_t*)malloc(i_buf_size);
    if (!p_fixed_buf)
        return NULL;

    memcpy(p_fixed_buf, p_buf, i_buf_size);
    p_fixed_buf[4] |= 0x03;
    data = CFDataCreate(kCFAllocatorDefault, p_fixed_buf, i_buf_size);
  }
  else
  {
    data = CFDataCreate(kCFAllocatorDefault, p_buf, i_buf_size);
  }

  return data;
}

static CFDataRef hevCCreate(const uint8_t *p_buf, int i_buf_size)
{
  CFDataRef data;
  // each NAL sent to the decoder is preceded by a 4 byte header
  // we need to change the hevC header to signal headers of 4 bytes, if needed
  if (i_buf_size >= 4 && (p_buf[4] & 0x03) != 0x03)
  {
    uint8_t *p_fixed_buf = (uint8_t*)malloc(i_buf_size);
    if (!p_fixed_buf)
        return NULL;

    memcpy(p_fixed_buf, p_buf, i_buf_size);
    p_fixed_buf[4] |= 0x03;
    data = CFDataCreate(kCFAllocatorDefault, p_fixed_buf, i_buf_size);
  }
  else
  {
    data = CFDataCreate(kCFAllocatorDefault, p_buf, i_buf_size);
  }

  return data;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
// helper function that wraps dts/pts into a dictionary
static CFDictionaryRef
CreateDictionaryWithDisplayTime(int64_t time, double dts, double pts)
{
  CFStringRef key[3] = {
    CFSTR("VideoDisplay_TIME"),
    CFSTR("VideoDisplay_DTS"),
    CFSTR("VideoDisplay_PTS")};
  CFNumberRef value[3];
  CFDictionaryRef display_time;

  value[0] = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongLongType, &time);
  value[1] = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &dts);
  value[2] = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &pts);

  display_time = CFDictionaryCreate(
    kCFAllocatorDefault, (const void **)&key, (const void **)&value, 3,
    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  
  CFRelease(value[0]);
  CFRelease(value[1]);
  CFRelease(value[2]);

  return display_time;
}
// helper function to extract dts/pts from a dictionary
static void
GetFrameDisplayTimeFromDictionary(
  CFDictionaryRef inFrameInfoDictionary, frame_queue *frame)
{
  // default to DVD_NOPTS_VALUE
  frame->sort_time = -1;
  frame->dts = DVD_NOPTS_VALUE;
  frame->pts = DVD_NOPTS_VALUE;
  if (inFrameInfoDictionary == NULL)
    return;

  CFNumberRef value[3];
  //
  value[0] = (CFNumberRef)CFDictionaryGetValue(inFrameInfoDictionary, CFSTR("VideoDisplay_TIME"));
  if (value[0])
    CFNumberGetValue(value[0], kCFNumberLongLongType, &frame->sort_time);
  value[1] = (CFNumberRef)CFDictionaryGetValue(inFrameInfoDictionary, CFSTR("VideoDisplay_DTS"));
  if (value[1])
    CFNumberGetValue(value[1], kCFNumberDoubleType, &frame->dts);
  value[2] = (CFNumberRef)CFDictionaryGetValue(inFrameInfoDictionary, CFSTR("VideoDisplay_PTS"));
  if (value[2])
    CFNumberGetValue(value[2], kCFNumberDoubleType, &frame->pts);

  return;
}
// helper function to create a format descriptor
static CMFormatDescriptionRef
CreateFormatDescription(VTFormatId format_id, int width, int height)
{
  CMFormatDescriptionRef fmt_desc;
  OSStatus status;

  status = CMVideoFormatDescriptionCreate(
    NULL,             // CFAllocatorRef allocator
    format_id,
    width,
    height,
    NULL,             // CFDictionaryRef extensions
    &fmt_desc);

  if (status == kVTDecoderNoErr)
    return fmt_desc;
  else
    return NULL;
}
// helper function to create a avcC/hevC/esds atom format descriptor
static CMFormatDescriptionRef
CreateFormatDescriptionFromCodecData(VTFormatId format_id, int width, int height, const uint8_t *extradata, int extradata_size, bool interlaced)
{
  CFMutableDictionaryRef pixelAspectRatio = CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
    &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetSInt32(pixelAspectRatio, CFSTR("HorizontalSpacing"), width);
  CFDictionarySetSInt32(pixelAspectRatio, CFSTR("VerticalSpacing")  , height);

  CFMutableDictionaryRef atoms = CFDictionaryCreateMutable(NULL,
    0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

  if (format_id == kVTFormatH264)
  {
    CFDataRef avcCData = avvCCreate(extradata, extradata_size);
    CFDictionarySetValue(atoms, CFSTR ("avcC"), avcCData);
    CFRelease(avcCData);
  }
  else if (format_id == kVTFormatH265)
  {
    CFDataRef hevCData = hevCCreate(extradata, extradata_size);
    CFDictionarySetValue(atoms, CFSTR ("hvcC"), hevCData);
    CFRelease(hevCData);
  }
  else if (format_id == kVTFormatMPEG4Video)
  {
    CFDataRef esdsData = ESDSCreate(extradata, extradata_size);
    CFDictionarySetValue(atoms, CFSTR ("esds"), esdsData);
    CFRelease(esdsData);
  }
  else
  {
    return NULL;
  }

  CFMutableDictionaryRef extensions = CFDictionaryCreateMutable (NULL,
    0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFMutableDictionarySetString(extensions, CFSTR("CVImageBufferChromaLocationBottomField"), "left");
  CFMutableDictionarySetString(extensions, CFSTR("CVImageBufferChromaLocationTopField"), "left");
  CFDictionarySetValue(extensions, CFSTR("FullRangeVideo"), kCFBooleanFalse);
  CFMutableDictionarySetObject(extensions, CFSTR("CVPixelAspectRatio"), (CFTypeRef*)pixelAspectRatio);
  CFMutableDictionarySetObject(extensions, CFSTR("SampleDescriptionExtensionAtoms"), (CFTypeRef*)atoms);
  if (interlaced)
  {
    //CFDictionarySetValue(decoderConfiguration, kVTDecompressionPropertyKey_FieldMode, kVTDecompressionProperty_FieldMode_DeinterlaceFields);
    //CFDictionarySetValue(decoderConfiguration, kVTDecompressionPropertyKey_DeinterlaceMode, kVTDecompressionProperty_DeinterlaceMode_Temporal);
    CFMutableDictionarySetString(extensions, CFSTR("FieldMode"), "DeinterlaceFields");
    CFMutableDictionarySetString(extensions, CFSTR("DeinterlaceMode"), "Temporal");
  }

  CMFormatDescriptionRef fmt_desc = NULL;
  OSStatus status = CMVideoFormatDescriptionCreate(NULL, format_id, width, height, extensions, &fmt_desc);
  if (status == kVTDecoderNoErr)
    return fmt_desc;
  else
    return NULL;
}
// helper function to create a CMSampleBufferRef from demuxer data
static CMSampleBufferRef
CreateSampleBufferFrom(CMFormatDescriptionRef fmt_desc,
  CMSampleTimingInfo *timingInfo, void *demux_buff, size_t demux_size)
{
  // need to retain the demux data until decoder is done with it.
  // the best way to do this is malloc/memcpy and use a kCFAllocatorMalloc.
  size_t demuxSize = demux_size;
  uint8_t *demuxData = (uint8_t*)malloc(demuxSize);
  memcpy(demuxData, demux_buff, demuxSize);

  CMBlockBufferRef videoBlock = nullptr;
  CMBlockBufferFlags flags = 0;
  OSStatus status = CMBlockBufferCreateWithMemoryBlock(
    kCFAllocatorDefault,  // CFAllocatorRef structureAllocator
    demuxData,            // void *memoryBlock
    demuxSize,            // size_t blockLength
    kCFAllocatorMalloc,   // CFAllocatorRef blockAllocator
    nullptr,              // const CMBlockBufferCustomBlockSource *customBlockSource
    0,                    // size_t offsetToData
    demux_size,           // size_t dataLength
    flags,                // CMBlockBufferFlags flags
    &videoBlock);         // CMBlockBufferRef

  CMSampleBufferRef sBufOut = nullptr;
  const size_t sampleSizeArray[] = {demuxSize};

  if (status == noErr)
  {
    status = CMSampleBufferCreate(
      kCFAllocatorDefault,// CFAllocatorRef allocator
      videoBlock,         // CMBlockBufferRef dataBuffer
      true,               // Boolean dataReady
      nullptr,            // CMSampleBufferMakeDataReadyCallback makeDataReadyCallback
      nullptr,            // void *makeDataReadyRefcon
      fmt_desc,           // CMFormatDescriptionRef formatDescription
      1,                  // CMItemCount numSamples
      1,                  // CMItemCount numSampleTimingEntries
      timingInfo,         // const CMSampleTimingInfo *sampleTimingArray
      1,                  // CMItemCount numSampleSizeEntries
      sampleSizeArray,    // const size_t *sampleSizeArray
      &sBufOut);          // CMSampleBufferRef *sBufOut
  }
  CFRelease(videoBlock);

  return sBufOut;
}

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
CDVDVideoCodecVideoToolBox::CDVDVideoCodecVideoToolBox(CProcessInfo &processInfo) : CDVDVideoCodec(processInfo)
{
  m_fmt_desc    = NULL;
  m_bitstream   = NULL;
  m_vt_session  = NULL;
  m_pFormatName = "vtb";

  m_queue_depth = 0;
  m_display_queue = NULL;
  m_max_ref_frames = 4;
  pthread_mutex_init(&m_queue_mutex, NULL);

  memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));
  m_DropPictures = false;
  m_codecControlFlags = 0;
  m_sort_time = 0;
  m_started = false;
  m_lastIDRframe = 0;
  m_sessionRestart = false;
  m_sessionRestartDTS = DVD_NOPTS_VALUE;
  m_sessionRestartPTS = DVD_NOPTS_VALUE;
  m_enable_temporal_processing = false;
  
  m_dll = new DllVideoToolBox();
}

CDVDVideoCodecVideoToolBox::~CDVDVideoCodecVideoToolBox()
{
  Dispose();
  pthread_mutex_destroy(&m_queue_mutex);
  SAFE_DELETE(m_dll);
}

bool CDVDVideoCodecVideoToolBox::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  if (CSettings::GetInstance().GetBool(CSettings::SETTING_VIDEOPLAYER_USEVTB) && !hints.software)
  {
    if (hints.codec != AV_CODEC_ID_H264)
    {
      return false;
    }
    
    CBitstreamConverter bs;
    if (!bs.Open(hints.codec, (uint8_t*)hints.extradata, hints.extrasize, false))
    {
      return false;
    }
    CFDataRef avcCData = CFDataCreate(kCFAllocatorDefault,
                                      (const uint8_t*)bs.GetExtraData(), bs.GetExtraSize());
    bool interlaced = true;
    int max_ref_frames;
    uint8_t *spc = (uint8_t*)CFDataGetBytePtr(avcCData) + 6;
    uint32_t sps_size = BS_RB16(spc);
    if (sps_size)
      bs.parseh264_sps(spc+3, sps_size-1, &interlaced, &max_ref_frames);
    CFRelease(avcCData);
      
    if (interlaced)
    {
      CLog::Log(LOGNOTICE, "%s - interlaced content.", __FUNCTION__);
      return false;
    }

    int width  = hints.width;
    int height = hints.height;
    int level  = hints.level;
    int profile = hints.profile;
    unsigned int extrasize = hints.extrasize; // extra data for codec to use
    uint8_t *extradata = (uint8_t*)hints.extradata; // size of extra data
 
#if defined(TARGET_DARWIN_IOS)
    if (CDarwinUtils::GetIOSVersion() < 6.0)
    {
      // under below iOS 5, VideoToolbox.framework is private
      m_dll->SetFile("/System/Library/PrivateFrameworks/VideoToolbox.framework/VideoToolbox");
    }
#endif
    if (!m_dll->IsLoaded() && !m_dll->Load())
    {
      CLog::Log(LOGERROR,"VideoToolBox: Error loading VideoToolBox framework (%s).",__FUNCTION__);
      return false;
    }

    switch(profile)
    {
      case FF_PROFILE_H264_HIGH_10:
      case FF_PROFILE_H264_HIGH_10_INTRA:
      case FF_PROFILE_H264_HIGH_422:
      case FF_PROFILE_H264_HIGH_422_INTRA:
      case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
      case FF_PROFILE_H264_HIGH_444_INTRA:
      case FF_PROFILE_H264_CAVLC_444:
        CLog::Log(LOGNOTICE, "%s - unsupported h264 profile(%d)", __FUNCTION__, hints.profile);
        return false;
        break;
    }

    if (width <= 0 || height <= 0)
    {
      CLog::Log(LOGNOTICE, "%s - bailing with bogus hints, width(%d), height(%d)",
        __FUNCTION__, width, height);
      return false;
    }
    
    switch (hints.codec)
    {
      case AV_CODEC_ID_MPEG4:
        return false;
        m_fmt_desc = CreateFormatDescriptionFromCodecData(
          kVTFormatMPEG4Video, width, height, extradata, extrasize, false);
        m_pFormatName = "vtb-mpeg4";
      break;

      case AV_CODEC_ID_MPEG2VIDEO:
        return false;
        m_fmt_desc = CreateFormatDescription(kVTFormatMPEG2Video, width, height);
        m_pFormatName = "vtb-mpeg2";
      break;

      case AV_CODEC_ID_H265:
        return false;
        // use a bitstream converter for all flavors
        m_bitstream = new CBitstreamConverter;
        if (!m_bitstream->Open(hints.codec, (uint8_t*)hints.extradata, hints.extrasize, false))
          return false;
        m_fmt_desc = CreateFormatDescriptionFromCodecData(
          kVTFormatH265, width, height, extradata, extrasize, false);
        m_pFormatName = "vtb-h265";
      break;

      case AV_CODEC_ID_H264:
        if (extrasize < 7 || extradata == NULL)
        {
          CLog::Log(LOGNOTICE, "%s - avcC atom too data small or missing", __FUNCTION__);
          return false;
        }
        else
        {
          // use a bitstream converter for all flavors, that way
          // even avcC with silly 3-byte nals are covered.
          m_bitstream = new CBitstreamConverter;
          if (!m_bitstream->Open(hints.codec, (uint8_t*)hints.extradata, hints.extrasize, false))
            return false;

          if (m_bitstream->GetExtraSize() < 8)
          {
            SAFE_DELETE(m_bitstream);
            return false;
          }
          else
          {
            // check the avcC atom's sps for number of reference frames
            // ignore if interlaced, it's handled in hints check above (until we get it working :)
            bool interlaced = true;
            uint8_t *spc = m_bitstream->GetExtraData() + 6;
            uint32_t sps_size = BS_RB16(spc);
            if (sps_size)
              m_bitstream->parseh264_sps(spc+3, sps_size-1, &interlaced, &m_max_ref_frames);
          }

          if (profile == FF_PROFILE_H264_MAIN && level == 32 && m_max_ref_frames > 4)
          {
            // Main@L3.2, VTB cannot handle greater than 4 reference frames
            CLog::Log(LOGNOTICE, "%s - Main@L3.2 detected, VTB cannot decode.", __FUNCTION__);
            return false;
          }

          m_fmt_desc = CreateFormatDescriptionFromCodecData(
            kVTFormatH264, width, height, m_bitstream->GetExtraData(), m_bitstream->GetExtraSize(), m_enable_temporal_processing);

          CLog::Log(LOGNOTICE, "%s - using avcC atom of size(%d), ref_frames(%d)", __FUNCTION__, extrasize, m_max_ref_frames);
        }
        m_pFormatName = "vtb-h264";
      break;

      default:
        return false;
      break;
    }

    if (m_fmt_desc == NULL)
    {
      CLog::Log(LOGNOTICE, "%s - created format descriptor", __FUNCTION__);
      m_pFormatName = "";
      return false;
    }

    if (m_max_ref_frames == 0)
      m_max_ref_frames = 2;

    CreateVTSession(width, height, m_fmt_desc);
    if (m_vt_session == NULL)
    {
      if (m_fmt_desc)
      {
        CFRelease(m_fmt_desc);
        m_fmt_desc = NULL;
      }
      m_pFormatName = "";
      return false;
    }

    // setup a DVDVideoPicture buffer.
    // first make sure all properties are reset.
    memset(&m_videobuffer, 0, sizeof(DVDVideoPicture));

    m_videobuffer.dts = DVD_NOPTS_VALUE;
    m_videobuffer.pts = DVD_NOPTS_VALUE;
    m_videobuffer.format = RENDER_FMT_CVBREF;
    m_videobuffer.color_range  = 0;
    m_videobuffer.color_matrix = 4;
    m_videobuffer.iFlags  = DVP_FLAG_ALLOCATED;
    m_videobuffer.iWidth  = hints.width;
    m_videobuffer.iHeight = hints.height;
    m_videobuffer.iDisplayWidth  = hints.width;
    m_videobuffer.iDisplayHeight = hints.height;

    m_DropPictures = false;
    // default to 5 min, this helps us feed correct pts to the player.
    m_max_ref_frames = std::max(m_max_ref_frames + 1, 5);
    m_sort_time = 0;

    CLog::Log(LOGDEBUG,"VideoToolBox: opened width(%d), height(%d)", hints.width, hints.height);
    m_hintsForReopen = hints;
    m_optionsForReopen = options;
    
    m_processInfo.SetVideoDeintMethod("none");
    
    std::list<EINTERLACEMETHOD> deintMethods;
    deintMethods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_NONE);
    m_processInfo.UpdateDeinterlacingMethods(deintMethods);

    m_processInfo.SetVideoDecoderName(m_pFormatName, true);
    m_processInfo.SetVideoDimensions(hints.width, hints.height);
    m_processInfo.SetVideoPixelFormat("420YpCbCr8BiPlanarVideoRange");
    m_processInfo.SetVideoDeintMethod("none");
    m_processInfo.SetVideoDAR(hints.aspect);

    return true;
  }

  return false;
}

void CDVDVideoCodecVideoToolBox::Dispose()
{
  DestroyVTSession();
  if (m_fmt_desc)
    CFRelease(m_fmt_desc), m_fmt_desc = NULL;
  SAFE_DELETE(m_bitstream);
 
  if (m_videobuffer.iFlags & DVP_FLAG_ALLOCATED)
  {
    // release any previous retained cvbuffer reference
    if (m_videobuffer.cvBufferRef)
      CVBufferRelease(m_videobuffer.cvBufferRef);
    m_videobuffer.cvBufferRef = NULL;
    m_videobuffer.iFlags = 0;
  }
  
  while (m_queue_depth)
    DisplayQueuePop();
}

void CDVDVideoCodecVideoToolBox::Reopen()
{
  m_started = false;
  Dispose();
  Open(m_hintsForReopen, m_optionsForReopen);
}

void CDVDVideoCodecVideoToolBox::SetDropState(bool bDrop)
{
  // more a message to decoder to hurry up.
  // VideoToolBox has no such ability so ignore it.
  m_DropPictures = bDrop;
}

int CDVDVideoCodecVideoToolBox::Decode(uint8_t* pData, int iSize, double dts, double pts)
{
  if (m_codecControlFlags & DVD_CODEC_CTRL_DRAIN)
  {
    if (m_queue_depth > 0)
      return VC_PICTURE;
    else
      return VC_BUFFER;
  }

  if (pData)
  {
    if (m_bitstream)
    {
      m_bitstream->Convert(pData, iSize);
      iSize = m_bitstream->GetConvertSize();
      pData = m_bitstream->GetConvertBuffer();
    }

    if (CBitstreamParser::FindIdrSlice(pData, iSize, false))
    {
      // VideoToolBox is picky about starting up with 1st frame as IDR slice
      // Check and skip until we hit one. m_lastIDRframe tracks how many frames back
      // was the last IDR + max ref frames. It is used during reset and reopen.
      //CLog::Log(LOGDEBUG, "%s - IDR Slice found, m_lastIDRframe %d", __FUNCTION__, m_lastIDRframe);
      m_started = true;
      m_lastIDRframe = 0;
    }
    m_lastIDRframe++;

    if (!m_started)
      return VC_BUFFER;

    CMSampleTimingInfo sampleTimingInfo = kCMTimingInfoInvalid;
    if (dts != DVD_NOPTS_VALUE)
      sampleTimingInfo.decodeTimeStamp = CMTimeMake(dts, DVD_TIME_BASE);
    if (pts != DVD_NOPTS_VALUE)
      sampleTimingInfo.presentationTimeStamp = CMTimeMake(pts, DVD_TIME_BASE);

    CMSampleBufferRef sampleBuff = CreateSampleBufferFrom(m_fmt_desc, &sampleTimingInfo, pData, iSize);
    if (!sampleBuff)
    {
      CLog::Log(LOGNOTICE, "%s - CreateSampleBufferFrom failed", __FUNCTION__);
      return VC_ERROR;
    }
    
    CFDictionaryRef frameInfo = CreateDictionaryWithDisplayTime(m_sort_time++, dts, pts);

    uint32_t decoderFlags = 0;
    if (m_enable_temporal_processing)
      decoderFlags |= kVTDecodeInfo_EnableTemporalProcessing;

    OSStatus status = m_dll->VTDecompressionSessionDecodeFrame(m_vt_session, sampleBuff, decoderFlags, frameInfo, 0);
    if (status != kVTDecoderNoErr)
    {
      CFRelease(frameInfo);
      CFRelease(sampleBuff);
      // might not really be an error (could have been force inactive)
      // so do not log it.
      if (status == kVTInvalidSessionErr)
      {
        m_sessionRestart = true;
        m_sessionRestartDTS = dts;
        m_sessionRestartPTS = pts;
        if (m_display_queue)
        {
          m_sessionRestartDTS = m_display_queue->dts;
          m_sessionRestartPTS = m_display_queue->pts;
        }
        return VC_REOPEN;
      }

      CLog::Log(LOGNOTICE, "%s - VTDecompressionSessionDecodeFrame returned(%d)",
        __FUNCTION__, (int)status);
      if (status == kVTVideoDecoderMalfunctionErr)
        return VC_REOPEN;
      else
        return VC_ERROR;
      // VTDecompressionSessionDecodeFrame returned 8969 (codecBadDataErr)
      // VTDecompressionSessionDecodeFrame returned -12350
      // VTDecompressionSessionDecodeFrame returned -12902 (kVTParameterErr)
      // VTDecompressionSessionDecodeFrame returned -12903 (kVTInvalidSessionErr)
      // VTDecompressionSessionDecodeFrame returned -12909 (kVTVideoDecoderBadDataErr)
      // VTDecompressionSessionDecodeFrame returned -12911 (kVTVideoDecoderMalfunctionErr)
    }
/*
    // wait for decoding to finish
    status = m_dll->VTDecompressionSessionWaitForAsynchronousFrames(m_vt_session);
    if (status != kVTDecoderNoErr)
    {
      CLog::Log(LOGNOTICE, "%s - VTDecompressionSessionWaitForAsynchronousFrames returned(%d)",
        __FUNCTION__, (int)status);
      CFRelease(frameInfo);
      CFRelease(sampleBuff);
      return VC_ERROR;
    }
*/
    CFRelease(frameInfo);
    CFRelease(sampleBuff);

    // put a limit on convergence count to avoid
    // huge mem usage on streams without keyframes
    if (m_lastIDRframe > 300)
    {
      CLog::Log(LOGNOTICE, "%s - m_lastIDRframe (%i) clamped ", __FUNCTION__, m_lastIDRframe);
      m_lastIDRframe = 300;
    }
  }

  if (m_queue_depth < (2 * m_max_ref_frames))
    return VC_BUFFER;

  return VC_PICTURE;
}

void CDVDVideoCodecVideoToolBox::Reset(void)
{
  // flush decoder
  m_dll->VTDecompressionSessionWaitForAsynchronousFrames(m_vt_session);

  while (m_queue_depth)
    DisplayQueuePop();
  
  m_sort_time = 0;
  m_codecControlFlags = 0;
}

unsigned CDVDVideoCodecVideoToolBox::GetConvergeCount()
{
  return m_lastIDRframe;
}

unsigned CDVDVideoCodecVideoToolBox::GetAllowedReferences()
{
  return 5;
}

void CDVDVideoCodecVideoToolBox::SetCodecControl(int flags)
{
  m_codecControlFlags = flags;
}

bool CDVDVideoCodecVideoToolBox::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  // clone the video picture buffer settings.
  *pDvdVideoPicture = m_videobuffer;

  // get the top picture frame, we risk getting the wrong frame if the frame queue
  // depth is less than the number of encoded reference frames. If queue depth
  // is greater than the number of encoded reference frames, then the top frame
  // will never change and we can just grab a ref to the top frame. This way
  // we don't lockout the vdadecoder while doing color format convert.
  pthread_mutex_lock(&m_queue_mutex);
  pDvdVideoPicture->dts             = m_display_queue->dts;
  pDvdVideoPicture->pts             = m_display_queue->pts;
  pDvdVideoPicture->iFlags          = DVP_FLAG_ALLOCATED;
  pDvdVideoPicture->iWidth          = (unsigned int)m_display_queue->width;
  pDvdVideoPicture->iHeight         = (unsigned int)m_display_queue->height;
  pDvdVideoPicture->iDisplayWidth   = (unsigned int)m_display_queue->width;
  pDvdVideoPicture->iDisplayHeight  = (unsigned int)m_display_queue->height;
  pDvdVideoPicture->cvBufferRef     = m_display_queue->pixel_buffer_ref;
  m_display_queue->pixel_buffer_ref = NULL;
  pthread_mutex_unlock(&m_queue_mutex);

  // now we can pop the top frame
  DisplayQueuePop();

  static double old_pts;
  if (g_advancedSettings.CanLogComponent(LOGVIDEO) && pDvdVideoPicture->pts < old_pts)
    CLog::Log(LOGDEBUG, "%s - VTBDecoderDecode dts(%f), pts(%f), old_pts(%f)", __FUNCTION__,
      pDvdVideoPicture->dts, pDvdVideoPicture->pts, old_pts);
  old_pts = pDvdVideoPicture->pts;
  
//  CLog::Log(LOGDEBUG, "%s - VTBDecoderDecode dts(%f), pts(%f), cvBufferRef(%p)", __FUNCTION__,
//    pDvdVideoPicture->dts, pDvdVideoPicture->pts, pDvdVideoPicture->cvBufferRef);

  if (m_codecControlFlags & DVD_CODEC_CTRL_DROP)
    pDvdVideoPicture->iFlags |= DVP_FLAG_DROPPED;

  // if vtb session restarts, we start decoding at last IDR frame
  // but dvdplay/renderer will show frames in fast forward style
  // until we hit sync point. Visually anoying so we force those
  // frames to get dropped and not shown.
  if (m_sessionRestart)
  {
    if (m_sessionRestartDTS == pDvdVideoPicture->dts &&
        m_sessionRestartPTS == pDvdVideoPicture->pts)
    {
      m_sessionRestart = false;
    }
    else
    {
      pDvdVideoPicture->iFlags |= DVP_FLAG_DROPPED;
    }
  }

  return true;
}

bool CDVDVideoCodecVideoToolBox::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  // release any previous retained image buffer ref that
  // has not been passed up to renderer (ie. dropped frames, etc).
  if (pDvdVideoPicture->cvBufferRef)
    CVBufferRelease(pDvdVideoPicture->cvBufferRef);

  return CDVDVideoCodec::ClearPicture(pDvdVideoPicture);
}

void CDVDVideoCodecVideoToolBox::DisplayQueuePop(void)
{
  if (!m_display_queue || m_queue_depth == 0)
    return;

  // pop the top frame off the queue
  pthread_mutex_lock(&m_queue_mutex);
  frame_queue *top_frame = m_display_queue;
  m_display_queue = m_display_queue->nextframe;
  m_queue_depth--;
  pthread_mutex_unlock(&m_queue_mutex);

  // and release it
  if (top_frame->pixel_buffer_ref)
    CVBufferRelease(top_frame->pixel_buffer_ref);
  free(top_frame);
}


void
CDVDVideoCodecVideoToolBox::CreateVTSession(int width, int height, CMFormatDescriptionRef fmt_desc)
{
  VTDecompressionSessionRef vt_session = NULL;
  CFMutableDictionaryRef destinationPixelBufferAttributes;
  VTDecompressionOutputCallback outputCallback;
  OSStatus status;

#if defined(TARGET_DARWIN_TVOS)
  // decoding, scaling and rendering 4k h264 runs into
  // some bandwidth limit. detect and scale down to reduce
  // the bandwidth requirements.
  int width_clamp = 1920;
  int new_width = CheckNP2(width);
  if (width != new_width)
  {
    // force picture width to power of two and scale up height
    // we do this because no GL_UNPACK_ROW_LENGTH in OpenGLES
    // and the CVPixelBufferPixel gets created using some
    // strange alignment when width is non-standard.
    double w_scaler = (double)new_width / width;
    width = new_width;
    height = height * w_scaler;
  }
  // scale output pictures down to 1080p size for display
  if (width > width_clamp)
  {
    double w_scaler = (float)width_clamp / width;
    width = width_clamp;
    height = height * w_scaler;
  }

#elif defined(TARGET_DARWIN_IOS)
  double scale = 0.0;

  // decoding, scaling and rendering above 1920 x 800 runs into
  // some bandwidth limit. detect and scale down to reduce
  // the bandwidth requirements.
  int width_clamp = 1280;
  if ((width * height) > (1920 * 800))
    width_clamp = 960;

  // for retina devices it should be safe [tm] to
  // loosen the clamp a bit to 1280 pixels width
  if (CDarwinUtils::DeviceHasRetina(scale))
    width_clamp = 1280;

  int new_width = CheckNP2(width);
  if (width != new_width)
  {
    // force picture width to power of two and scale up height
    // we do this because no GL_UNPACK_ROW_LENGTH in OpenGLES
    // and the CVPixelBufferPixel gets created using some
    // strange alignment when width is non-standard.
    double w_scaler = (double)new_width / width;
    width = new_width;
    height = height * w_scaler;
  }
#if !defined(__LP64__)
  // scale output pictures down to 720p size for display
  if (width > width_clamp)
  {
    double w_scaler = (float)width_clamp / width;
    width = width_clamp;
    height = height * w_scaler;
  }
#endif
#endif
  destinationPixelBufferAttributes = CFDictionaryCreateMutable(
    NULL, // CFAllocatorRef allocator
    0,    // CFIndex capacity
    &kCFTypeDictionaryKeyCallBacks,
    &kCFTypeDictionaryValueCallBacks);

  // The recommended pixel format choices are 
  //  kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange or kCVPixelFormatType_32BGRA.
  //  TODO: figure out what we need.
//  CFDictionarySetSInt32(destinationPixelBufferAttributes,
//    kCVPixelBufferPixelFormatTypeKey, kCVPixelFormatType_32BGRA);
  CFDictionarySetSInt32(destinationPixelBufferAttributes,
                        kCVPixelBufferPixelFormatTypeKey, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
  CFDictionarySetSInt32(destinationPixelBufferAttributes,
    kCVPixelBufferWidthKey, width);
  CFDictionarySetSInt32(destinationPixelBufferAttributes,
    kCVPixelBufferHeightKey, height);
  //CFDictionarySetValue(destinationPixelBufferAttributes,
  //  kCVPixelBufferOpenGLCompatibilityKey, kCFBooleanTrue);

  outputCallback.callback = VTDecoderCallback;
  outputCallback.refcon = this;

  status = m_dll->VTDecompressionSessionCreate(
    NULL, // CFAllocatorRef allocator
    fmt_desc,
    NULL, // CFTypeRef sessionOptions
    destinationPixelBufferAttributes,
    &outputCallback,
    &vt_session);
  if (status != noErr)
  {
    m_vt_session = NULL;
    CLog::Log(LOGERROR, "%s - failed with status = (%d)", __FUNCTION__, (int)status);
    // -12906, kVTCouldNotFindVideoDecoderErr
  }
  else
  {
    //vtdec_session_dump_properties(vt_session);
    m_vt_session = (void*)vt_session;
  }

  CFRelease(destinationPixelBufferAttributes);
}

void
CDVDVideoCodecVideoToolBox::DestroyVTSession(void)
{
  if (m_vt_session)
  {
    m_dll->VTDecompressionSessionInvalidate((VTDecompressionSessionRef)m_vt_session);
    CFRelease((VTDecompressionSessionRef)m_vt_session);
    m_vt_session = NULL;
  }
}

void 
CDVDVideoCodecVideoToolBox::VTDecoderCallback(
  void               *refcon,
  CFDictionaryRef    frameInfo,
  OSStatus           status,
  UInt32             infoFlags,
  CVBufferRef        imageBuffer,
  CMTime             pts,
  CMTime             duration)
{
  // This is an sync callback due to VTDecompressionSessionWaitForAsynchronousFrames
  CDVDVideoCodecVideoToolBox *ctx = (CDVDVideoCodecVideoToolBox*)refcon;

  if (status != kVTDecoderNoErr)
  {
    //CLog::Log(LOGDEBUG, "%s - status error (%d)", __FUNCTION__, (int)status);
    return;
  }
  if (imageBuffer == NULL)
  {
    //CLog::Log(LOGDEBUG, "%s - imageBuffer is NULL", __FUNCTION__);
    return;
  }
  OSType format_type = CVPixelBufferGetPixelFormatType(imageBuffer);
  //if (format_type != kCVPixelFormatType_32BGRA)
  if (format_type != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
  {
    CLog::Log(LOGERROR, "%s - imageBuffer format is not 'BGRA',is reporting 0x%x",
      "VTDecoderCallback", (int)format_type);
    //return;
  }
  if (kVTDecodeInfo_FrameDropped & infoFlags)
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s - frame dropped", __FUNCTION__);
    return;
  }

  // allocate a new frame and populate it with some information.
  // this pointer to a frame_queue type keeps track of the newest decompressed frame
  // and is then inserted into a linked list of frame pointers depending on the display time
  // parsed out of the bitstream and stored in the frameInfo dictionary by the client
  frame_queue *newFrame = (frame_queue*)calloc(sizeof(frame_queue), 1);
  newFrame->nextframe = NULL;
  if (CVPixelBufferIsPlanar(imageBuffer) )
  {
    newFrame->width  = CVPixelBufferGetWidthOfPlane(imageBuffer, 0);
    newFrame->height = CVPixelBufferGetHeightOfPlane(imageBuffer, 0);
  }
  else
  {
    newFrame->width  = CVPixelBufferGetWidth(imageBuffer);
    newFrame->height = CVPixelBufferGetHeight(imageBuffer);
  }  
  newFrame->pixel_buffer_format = format_type;
  newFrame->pixel_buffer_ref = CVBufferRetain(imageBuffer);
  GetFrameDisplayTimeFromDictionary(frameInfo, newFrame);

  // since the frames we get may be in decode order rather than presentation order
  // our hypothetical callback places them in a queue of frames which will
  // hold them in display order for display on another thread
  pthread_mutex_lock(&ctx->m_queue_mutex);

  frame_queue base;
  base.nextframe = ctx->m_display_queue;
  frame_queue *ptr = &base;
  for(; ptr->nextframe; ptr = ptr->nextframe)
  {
    if(ptr->nextframe->pts == DVD_NOPTS_VALUE
    || newFrame->pts       == DVD_NOPTS_VALUE)
      continue;
    if(ptr->nextframe->pts > newFrame->pts)
      break;
  }
  /* insert after ptr */
  newFrame->nextframe = ptr->nextframe;
  ptr->nextframe = newFrame;

  /* update anchor if needed */
  if(newFrame->nextframe == ctx->m_display_queue)
    ctx->m_display_queue = newFrame;

  ctx->m_queue_depth++;
  //
  pthread_mutex_unlock(&ctx->m_queue_mutex);	
}

void CDVDVideoCodecVideoToolBox::vtdec_session_dump_property(CFStringRef prop_name, CFDictionaryRef prop_attrs, CDVDVideoCodecVideoToolBox *ctx)
{
  char     *name_str;
  OSStatus  status;
  CFTypeRef prop_value;

  name_str = vtutil_string_to_utf8(prop_name);
  if (name_str)
  {
    char *attrs_str = vtutil_object_to_string(prop_attrs);
    if (attrs_str && g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s = %s\n", name_str, attrs_str);
    free(attrs_str);
  }

  VTDecompressionSessionRef vt_session = (VTDecompressionSessionRef)ctx->m_vt_session;
  status = ctx->m_dll->VTDecompressionSessionCopyProperty(vt_session, prop_name, NULL, &prop_value);
  if (status == kVTDecoderNoErr)
  {
    char *value_str = vtutil_object_to_string(prop_value);
    if (value_str && g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s = %s\n", name_str, value_str);
    free(value_str);

    if (prop_value != NULL)
      CFRelease(prop_value);
  }
  else
  {
    if (g_advancedSettings.CanLogComponent(LOGVIDEO))
      CLog::Log(LOGDEBUG, "%s = <failed to query: %d>\n", name_str, (int)status);
  }

  free(name_str);
}

void CDVDVideoCodecVideoToolBox::vtdec_session_dump_properties()
{
  VTDecompressionSessionRef vt_session = (VTDecompressionSessionRef)m_vt_session;
  VTDumpDecompressionPropCtx dpc = { this, vt_session };
  CFDictionaryRef dict;
  OSStatus status;

  status = m_dll->VTDecompressionSessionCopySupportedPropertyDictionary(vt_session, &dict);
  if (status != kVTDecoderNoErr)
    goto error;
  CFDictionaryApplyFunction(dict, (CFDictionaryApplierFunction)vtdec_session_dump_property, &dpc);
  CFRelease(dict);

  return;

error:
  if (g_advancedSettings.CanLogComponent(LOGVIDEO))
    CLog::Log(LOGDEBUG, "failed to dump properties\n");
}

#endif
