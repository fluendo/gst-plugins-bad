/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oravnas@cisco.com>
 * Copyright (C) 2018 Fluendo S.A <ngarcia@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "osxscreencapsrc.h"
#include "coremediabuffer.h"
#include <gst/video/video.h>
#include <gst/interfaces/propertyprobe.h>
#include <IOKit/graphics/IOGraphicsLib.h>

#define DEFAULT_DO_STATS FALSE
#define DEVICE_FPS_N 25
#define DEVICE_FPS_D 1
#define BUFFER_QUEUE_SIZE 2


GST_DEBUG_CATEGORY (gst_osx_screen_cap_src_debug);
#define GST_CAT_DEFAULT gst_osx_screen_cap_src_debug

static GstStaticPadTemplate src_template= GST_STATIC_PAD_TEMPLATE ("src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("video/x-raw-yuv, "
  "format = (fourcc) { NV12, UYVY, YUY2 }, "
  "framerate = " GST_VIDEO_FPS_RANGE ", "
  "width = " GST_VIDEO_SIZE_RANGE ", "
  "height = " GST_VIDEO_SIZE_RANGE "; "
  "video/x-raw-rgb, "
  "bpp = (int) 32, "
  "depth = (int) 32, "
  "endianness = (int) BIG_ENDIAN, "
  "red_mask = (int) " GST_VIDEO_BYTE3_MASK_32 ", "
  "green_mask = (int) " GST_VIDEO_BYTE2_MASK_32 ", "
  "blue_mask = (int) " GST_VIDEO_BYTE1_MASK_32 ", "
  "alpha_mask = (int) " GST_VIDEO_BYTE4_MASK_32 ", "
  "framerate = " GST_VIDEO_FPS_RANGE ", "
  "width = " GST_VIDEO_SIZE_RANGE ", "
  "height = " GST_VIDEO_SIZE_RANGE "; ") );

typedef enum _QueueState
{
  NO_BUFFERS=1,
  HAS_BUFFER_OR_STOP_REQUEST,
}QueueState;

static GstPushSrcClass*parent_class;

@interface GstOutputBuffer : NSObject<AVCaptureVideoDataOutputSampleBufferDelegate>
{
  GstElement*element;
  GstBaseSrc*baseSrc;
  GstPushSrc*pushSrc;

  AVCaptureSession*session;
  AVCaptureInput*input;
  AVCaptureVideoDataOutput*output;
  AVCaptureDevice*device;

  dispatch_queue_t mainQueue;
  dispatch_queue_t workerQueue;
  NSConditionLock*bufQueueLock;
  NSMutableArray*bufQueue;
  BOOL stopRequest;

  GstCaps*caps;
  GstVideoFormat format;
  gint width,height;
  gint fps_n,fps_d;
  GstClockTime duration;
  guint64 offset;

  GstClockTime lastSampling;
  guint count;
}

-(id)init;
-(id)initWithSrc:(GstPushSrc*)src;
-(void)finalize;
-(BOOL)openScreenInput;
-(BOOL)openDevice;
-(void)closeDevice;
-(GstVideoFormat)getGstVideoFormat:(NSNumber*)pixel_format;
-(GstCaps*)getCaps;
-(BOOL)setCaps:(GstCaps*)new_caps;
-(BOOL)start;
-(BOOL)stop;
-(BOOL)unlock;
-(BOOL)unlockStop;
-(BOOL)query:(GstQuery*)query;
-(GstStateChangeReturn)changeState:(GstStateChange)transition;
-(GstFlowReturn)create:(GstBuffer**)buf;
-(void)timestampBuffer:(GstBuffer*)buf;
-(void)updateStatistics;
-(void)captureOutput:(AVCaptureOutput*)captureOutput didOutputSampleBuffer:(
      CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection*)
    connection;

@end @ implementation GstOutputBuffer;

-(id)init {
  return [self initWithSrc:NULL];
}

-(id)initWithSrc:(GstPushSrc*)src {
  if( (self=[super init]) ) {
    element=GST_ELEMENT_CAST (src);
    baseSrc=GST_BASE_SRC_CAST (src);
    pushSrc=src;
    mainQueue=
        dispatch_queue_create ("org.freedesktop.gstreamer.avfvideosrc.main",
      NULL);
    workerQueue=
        dispatch_queue_create ("org.freedesktop.gstreamer.avfvideosrc.output",
      NULL);
    gst_base_src_set_live (baseSrc,TRUE);
    gst_base_src_set_format (baseSrc,GST_FORMAT_TIME);
  }
  return self;
}

-(void)finalize {
  dispatch_release (mainQueue);
  mainQueue=NULL;
  dispatch_release (workerQueue);
  workerQueue=NULL;
}

-(BOOL)openScreenInput {

  GST_DEBUG_OBJECT (element,"Opening screen input");
  GstOSXScreenCapSrc*src;
  src=GST_OSX_SCREEN_CAPTURE(baseSrc);
  AVCaptureScreenInput*screenInput=
      [[AVCaptureScreenInput alloc]initWithDisplayID:src->displayId];
  @try{
    [screenInput setValue:[NSNumber numberWithBool:src->captureScreenCursor]
    forKey:@"capturesCursor"];
  }@catch(NSException*exception) {
    if(![[exception name]isEqualToString:NSUndefinedKeyException]) {
      GST_WARNING ("An unexpected error occured: %s",
          [[exception reason]UTF8String]);
    }
    GST_WARNING ("Capturing cursor is only supported in OS X >= 10.8");
  }
  screenInput.capturesMouseClicks=src->captureScreenMouseClicks;
  input=screenInput;
  [input retain];
  return YES;
}

-(BOOL)openDevice {
  BOOL success=NO,*successPtr=&success;

  GST_DEBUG_OBJECT (element,"Opening device");

  dispatch_sync (mainQueue,^{
    if(![self openScreenInput]) return;
    output=[[AVCaptureVideoDataOutput alloc]init];
    [output setSampleBufferDelegate:self queue:workerQueue];
    output.alwaysDiscardsLateVideoFrames=YES;
    output.videoSettings=nil;
    /* device native format */
    session=[[AVCaptureSession alloc]init];
    [session addInput:input];
    [session addOutput:output];
    *successPtr=YES;
  }
      );

  GST_DEBUG_OBJECT (element,"Opening device %s",
      success ? "succeed" : "failed");

  return success;
}

-(void)closeDevice {
  GST_DEBUG_OBJECT (element,"Closing device");
  dispatch_sync (mainQueue,^{
    g_assert (![session isRunning]);
    [session removeInput:input];
    [session removeOutput:output];
    [session release];
    session=nil;
    [input release];
    input=nil;
    [output release];
    output=nil;
    if(caps) gst_caps_unref (caps);
  }       );
}

-(GstVideoFormat)getGstVideoFormat:(NSNumber*)pixel_format {
  GstVideoFormat gst_format=GST_VIDEO_FORMAT_UNKNOWN;

  switch([pixel_format integerValue]) {
  case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange: /* 420v */
    gst_format=GST_VIDEO_FORMAT_NV12;
    break;
  case kCVPixelFormatType_422YpCbCr8: /* 2vuy */
    gst_format=GST_VIDEO_FORMAT_UYVY;
    break;
  case kCVPixelFormatType_32BGRA: /* BGRA */
    gst_format=GST_VIDEO_FORMAT_BGRA;
    break;

  case kCVPixelFormatType_422YpCbCr8_yuvs: /* yuvs */
    gst_format=GST_VIDEO_FORMAT_YUY2;
    break;

  default:
    GST_LOG_OBJECT (element,"Pixel format %s is not handled by osxscreencapsrc",
      [[pixel_format stringValue]UTF8String]);
    break;
  }
  GST_LOG_OBJECT (element,"Get value for pixel format %u -> %u",
      (int)[pixel_format integerValue],(int)gst_format);
  return gst_format;
}

-(GstCaps*)getCaps {
  GstCaps*result;
  NSArray*pixel_formats;

  if(session==nil) {
    return NULL; /* BaseSrc will return template caps */
  }
  result=gst_caps_new_empty ();
  pixel_formats=output.availableVideoCVPixelFormatTypes;
  GstOSXScreenCapSrc*src;
  src=GST_OSX_SCREEN_CAPTURE(baseSrc);

  CGRect rect=CGDisplayBounds (src->displayId);

  for(NSNumber*pixel_format in pixel_formats) {
    GstVideoFormat gst_format=[self getGstVideoFormat:pixel_format];
    if(gst_format!=GST_VIDEO_FORMAT_UNKNOWN) {
      gst_caps_append (result,gst_video_format_new_caps (gst_format,
          (int)rect.size.width,(int)rect.size.height,DEVICE_FPS_N,DEVICE_FPS_D,
          1,
          1) );

      GST_DEBUG_OBJECT (element,"Getting caps Width: %d Height: %d Format: %u", \
          (int)rect.size.width,(int)rect.size.height,gst_format);
    }
  }
  return result;
}

-(BOOL)setCaps:(GstCaps*)new_caps {
  BOOL success=YES,*successPtr=&success;

  gst_video_format_parse_caps (new_caps,&format,&width,&height);
  gst_video_parse_caps_framerate (new_caps,&fps_n,&fps_d);

  dispatch_sync (mainQueue,^{
    int newformat=kCVPixelFormatType_32BGRA;
    g_assert (![session isRunning]);
    AVCaptureScreenInput*screenInput=(AVCaptureScreenInput*)input;
    screenInput.minFrameDuration=CMTimeMake (fps_d,fps_n);
    switch(format) {
    case GST_VIDEO_FORMAT_NV12:
      newformat=kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      newformat=kCVPixelFormatType_422YpCbCr8;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      newformat=kCVPixelFormatType_422YpCbCr8_yuvs;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      newformat=kCVPixelFormatType_32BGRA;
      break;
    default:
      *successPtr=NO;
      GST_WARNING ("Unsupported output format %d",format);
      return;
    }

    GST_DEBUG_OBJECT (element,
    "Setting caps Width: %d Height: %d Format: %" GST_FOURCC_FORMAT, \
    width,height,GST_FOURCC_ARGS (gst_video_format_to_fourcc (format) ) );
    output.videoSettings=
    [NSDictionary dictionaryWithObject:[NSNumber numberWithInt:newformat]
    forKey:(NSString*)kCVPixelBufferPixelFormatTypeKey];
    caps=gst_caps_copy (new_caps); [session startRunning];
    /* Unlock device configuration only after session is started so the session
     * won't reset the capture formats */
    [device unlockForConfiguration];
  }
      );

  return success;
}

-(BOOL)start {
  bufQueueLock=[[NSConditionLock alloc]initWithCondition:NO_BUFFERS];
  bufQueue=[[NSMutableArray alloc]initWithCapacity:BUFFER_QUEUE_SIZE];
  stopRequest=NO;
  duration=gst_util_uint64_scale (GST_SECOND,DEVICE_FPS_D,DEVICE_FPS_N);
  offset=0;
  lastSampling=GST_CLOCK_TIME_NONE;
  count=0;

  return YES;
}

-(BOOL)stop {
  dispatch_sync (mainQueue,^{
    [session stopRunning];
  }
      );
  dispatch_sync (workerQueue,^{
  }
      );

  [bufQueueLock release];
  bufQueueLock=nil;
  [bufQueue release];
  bufQueue=nil;
  return YES;
}

-(BOOL)query:(GstQuery*)query {
  BOOL result=NO;

  if(GST_QUERY_TYPE (query)==GST_QUERY_LATENCY) {
    if(device!=nil) {
      GstClockTime min_latency,max_latency;

      min_latency=max_latency=duration; /* for now */
      result=YES;

      GST_DEBUG_OBJECT (element,"reporting latency of min %" GST_TIME_FORMAT
          " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency),GST_TIME_ARGS (max_latency) );
      gst_query_set_latency (query,TRUE,min_latency,max_latency);
    }
  }else{
    result=GST_BASE_SRC_CLASS (parent_class)->query (baseSrc,query);
  }
  return result;
}

-(BOOL)unlock {
  [bufQueueLock lock];
  stopRequest=YES;
  [bufQueueLock unlockWithCondition:HAS_BUFFER_OR_STOP_REQUEST];
  return YES;
}

-(BOOL)unlockStop {
  [bufQueueLock lock];
  stopRequest=NO;
  [bufQueueLock unlock];
  return YES;
}

-(GstStateChangeReturn)changeState:(GstStateChange)transition {
  GstStateChangeReturn ret;
  if(transition==GST_STATE_CHANGE_NULL_TO_READY) {
    if(![self openDevice])
      return GST_STATE_CHANGE_FAILURE;
  }
  ret=GST_ELEMENT_CLASS (parent_class)->change_state (element,transition);
  if(transition==GST_STATE_CHANGE_READY_TO_NULL)
    [self closeDevice];

  return ret;
}

-(void)captureOutput:(AVCaptureOutput*)captureOutput didOutputSampleBuffer:(
      CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection*)
    connection
{
  [bufQueueLock lock];
  if(stopRequest) {
    [bufQueueLock unlock];
    return;
  }
  if([bufQueue count]==BUFFER_QUEUE_SIZE)
    [bufQueue removeLastObject];
  [bufQueue insertObject:(id)sampleBuffer atIndex:0];
  [bufQueueLock unlockWithCondition:HAS_BUFFER_OR_STOP_REQUEST];
}

-(GstFlowReturn)create:(GstBuffer**)buf {
  CMSampleBufferRef sbuf;
  CVImageBufferRef image_buf;
  CVPixelBufferRef pixel_buf;
  size_t cur_width,cur_height;

  [bufQueueLock lockWhenCondition:HAS_BUFFER_OR_STOP_REQUEST];
  if(stopRequest) {
    [bufQueueLock unlock];
    return GST_FLOW_WRONG_STATE;
  }

  sbuf=(CMSampleBufferRef)[bufQueue lastObject];
  CFRetain (sbuf);
  [bufQueue removeLastObject];
  [bufQueueLock unlockWithCondition:
  ([bufQueue count]==0) ? NO_BUFFERS : HAS_BUFFER_OR_STOP_REQUEST];

  /* Check output frame size dimensions */
  image_buf=CMSampleBufferGetImageBuffer (sbuf);
  if(image_buf) {
    pixel_buf=(CVPixelBufferRef)image_buf;
    cur_width=CVPixelBufferGetWidth (pixel_buf);
    cur_height=CVPixelBufferGetHeight (pixel_buf);

    if(width!=cur_width||height!=cur_height) {
      /* Set new caps according to current frame dimensions */
      GST_WARNING
        ("Output frame size has changed %dx%d -> %dx%d, updating caps",width,
          height,(int)cur_width,(int)cur_height);
      width=cur_width;
      height=cur_height;
      gst_caps_set_simple (caps,
          "width",G_TYPE_INT,width,"height",G_TYPE_INT,height,NULL);
      gst_pad_set_caps (GST_BASE_SRC_PAD (baseSrc),caps);
    }
  }

  *buf=gst_core_media_buffer_new (sbuf);
  CFRelease (sbuf);

  [self timestampBuffer:*buf];
  GstOSXScreenCapSrc*src;
  src=GST_OSX_SCREEN_CAPTURE(baseSrc);
  if(src->doStats)
    [self updateStatistics];

  return GST_FLOW_OK;
}

-(void)timestampBuffer:(GstBuffer*)buf {
  GstClock*clock;
  GstClockTime timestamp;

  GST_OBJECT_LOCK (element);
  clock=GST_ELEMENT_CLOCK (element);
  if(clock!=NULL) {
    gst_object_ref (clock);
    timestamp=element->base_time;
  }else{
    timestamp=GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (element);

  if(clock!=NULL) {
    timestamp=gst_clock_get_time (clock)-timestamp;
    if(timestamp>duration)
      timestamp-=duration;
    else
      timestamp=0;

    gst_object_unref (clock);
    clock=NULL;
  }

  GST_BUFFER_OFFSET (buf)=offset++;
  GST_BUFFER_OFFSET_END (buf)=GST_BUFFER_OFFSET (buf)+1;
  GST_BUFFER_TIMESTAMP (buf)=timestamp;
  GST_BUFFER_DURATION (buf)=duration;
}

-(void)updateStatistics {
  GstClock*clock;
  GstOSXScreenCapSrc*src;
  src=GST_OSX_SCREEN_CAPTURE(baseSrc);
  GST_OBJECT_LOCK (element);
  clock=GST_ELEMENT_CLOCK (element);
  if(clock!=NULL)
    gst_object_ref (clock);
  GST_OBJECT_UNLOCK (element);

  if(clock!=NULL) {
    GstClockTime now=gst_clock_get_time (clock);
    gst_object_unref (clock);

    count++;

    if(GST_CLOCK_TIME_IS_VALID (lastSampling) ) {
      if(now-lastSampling>=GST_SECOND) {
        GST_OBJECT_LOCK (element);
        src->fps=count;
        GST_OBJECT_UNLOCK (element);

        g_object_notify (G_OBJECT (element),"fps");

        lastSampling=now;
        count=0;
      }
    }else{
      lastSampling=now;
    }
  }
}

@end
/*
 * Glue code
 */
enum
{
  PROP_0,
  PROP_DISPLAY_INDEX,
  PROP_DEVICE,
  PROP_DO_STATS,
  PROP_FPS,
  PROP_CAPTURE_SCREEN_CURSOR,
  PROP_CAPTURE_SCREEN_MOUSE_CLICKS
};

static void gst_osx_screen_cap_src_init_interfaces (GType type);
static void
gst_osx_screen_cap_src_type_add_device_property_probe_interface (GType type);

GST_BOILERPLATE_FULL (GstOSXScreenCapSrc,gst_osx_screen_cap_src,GstPushSrc,
    GST_TYPE_PUSH_SRC,gst_osx_screen_cap_src_init_interfaces);

static void gst_osx_screen_cap_src_finalize (GObject*obj);
static void gst_osx_screen_cap_src_get_property (GObject*object,
    guint prop_id,GValue*value,GParamSpec*pspec);
static void gst_osx_screen_cap_src_set_property (GObject*object,
    guint prop_id,const GValue*value,GParamSpec*pspec);
static GstStateChangeReturn gst_osx_screen_cap_src_change_state (GstElement*
    element,GstStateChange transition);
static GstCaps*gst_osx_screen_cap_src_get_caps (GstBaseSrc*basesrc);
static gboolean gst_osx_screen_cap_src_set_caps (GstBaseSrc*basesrc,
    GstCaps*   caps);
static gboolean gst_osx_screen_cap_src_start (GstBaseSrc*basesrc);
static gboolean gst_osx_screen_cap_src_stop (GstBaseSrc*basesrc);
static gboolean gst_osx_screen_cap_src_query (GstBaseSrc*basesrc,
    GstQuery*  query);
static gboolean gst_osx_screen_cap_src_unlock (GstBaseSrc*basesrc);
static gboolean gst_osx_screen_cap_src_unlock_stop (GstBaseSrc*basesrc);
static GstFlowReturn gst_osx_screen_cap_src_create (GstPushSrc*pushsrc,
    GstBuffer**buf);

static gboolean
gst_osx_screen_cap_src_iface_supported (GstImplementsInterface*iface,
    GType iface_type)
{
  return FALSE;
}

static void
gst_osx_screen_cap_src_interface_init (GstImplementsInterfaceClass*klass)
{
  /* default virtual functions */
  klass->supported=gst_osx_screen_cap_src_iface_supported;
}

static void
gst_osx_screen_cap_src_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_iface_info={
    (GInterfaceInitFunc)gst_osx_screen_cap_src_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);

  gst_osx_screen_cap_src_type_add_device_property_probe_interface (type);
}

static void
gst_osx_screen_cap_src_base_init (gpointer gclass)
{
  GstElementClass*element_class=GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "OSX Screen Capture Source","Source/Video",
      "Reads frames from MacOS screen",
      "Ole André Vadla Ravnås <oravnas@cisco.com>,\
      Fluendo SA <ngarcia@fluendo.com>");

  gst_element_class_add_static_pad_template (element_class,&src_template);
}

static void
gst_osx_screen_cap_src_class_init (GstOSXScreenCapSrcClass*klass)
{
  GObjectClass*gobject_class=G_OBJECT_CLASS (klass);
  GstElementClass*gstelement_class=GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass*gstbasesrc_class=GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass*gstpushsrc_class=GST_PUSH_SRC_CLASS (klass);

  gobject_class->finalize=gst_osx_screen_cap_src_finalize;
  gobject_class->get_property=gst_osx_screen_cap_src_get_property;
  gobject_class->set_property=gst_osx_screen_cap_src_set_property;

  gstelement_class->change_state=gst_osx_screen_cap_src_change_state;

  gstbasesrc_class->get_caps=gst_osx_screen_cap_src_get_caps;
  gstbasesrc_class->set_caps=gst_osx_screen_cap_src_set_caps;
  gstbasesrc_class->start=gst_osx_screen_cap_src_start;
  gstbasesrc_class->stop=gst_osx_screen_cap_src_stop;
  gstbasesrc_class->query=gst_osx_screen_cap_src_query;
  gstbasesrc_class->unlock=gst_osx_screen_cap_src_unlock;
  gstbasesrc_class->unlock_stop=gst_osx_screen_cap_src_unlock_stop;

  gstpushsrc_class->create=gst_osx_screen_cap_src_create;

  g_object_class_install_property (gobject_class,PROP_DISPLAY_INDEX,
      g_param_spec_int ("display-index","Display Index",
      "Display index",
      -1,G_MAXINT,0,
      G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class,PROP_DEVICE,
      g_param_spec_string ("device","Display name",
      "Human-readable name of the display device",
      NULL,G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS) );
  g_object_class_install_property (gobject_class,PROP_DO_STATS,
      g_param_spec_boolean ("do-stats","Enable statistics",
      "Enable logging of statistics",DEFAULT_DO_STATS,
      G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS) );
  g_object_class_install_property (gobject_class,PROP_FPS,
      g_param_spec_int ("fps","Frames per second",
      "Last measured framerate, if statistics are enabled",
      -1,G_MAXINT,-1,G_PARAM_READABLE|G_PARAM_STATIC_STRINGS) );

  g_object_class_install_property (gobject_class,PROP_CAPTURE_SCREEN_CURSOR,
      g_param_spec_boolean ("capture-screen-cursor","Capture screen cursor",
      "Enable cursor capture while capturing screen",FALSE,
      G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS) );
  g_object_class_install_property (gobject_class,
      PROP_CAPTURE_SCREEN_MOUSE_CLICKS,
      g_param_spec_boolean ("capture-screen-mouse-clicks",
      "Enable mouse clicks capture",
      "Enable mouse clicks capture while capturing screen",FALSE,
      G_PARAM_READWRITE|G_PARAM_STATIC_STRINGS) );
  GST_DEBUG_CATEGORY_INIT (gst_osx_screen_cap_src_debug,"osxscreencapsrc",0,
      "OSX screen capture source");
}

#define OBJC_CALLOUT_BEGIN() \
  NSAutoreleasePool*pool; \
\
  pool=[[NSAutoreleasePool alloc]init]
#define OBJC_CALLOUT_END() \
  [pool release]

NSString*
screenNameForDisplay (CGDirectDisplayID displayID)
{
  NSString*screenName=nil;
  NSDictionary*deviceInfo=
      (NSDictionary*)
      IODisplayCreateInfoDictionary (CGDisplayIOServicePort (displayID),
    kIODisplayOnlyPreferredName);
  NSDictionary*localizedNames=
      [deviceInfo objectForKey:[NSString stringWithUTF8String:
      kDisplayProductName]];

  if([localizedNames count]>0) {
    screenName=
        [[localizedNames objectForKey:[[localizedNames allKeys]objectAtIndex:0]]
        retain];
  }
  [deviceInfo release];
  return [screenName autorelease];
}

static void
gst_osx_screen_cap_src_init (GstOSXScreenCapSrc*     src,
    GstOSXScreenCapSrcClass*gclass)
{
  OBJC_CALLOUT_BEGIN ();

  src->outputBuffer=[[GstOutputBuffer alloc]initWithSrc:GST_PUSH_SRC (src)];
  src->displayId=kCGDirectMainDisplay;
  src->displayName=NULL;
  src->m_displayCount=0;
  src->doStats=FALSE;
  src->captureScreenCursor=FALSE;
  src->captureScreenMouseClicks=FALSE;

  /*
     Fills array with all display devices
   */
  GST_DEBUG_OBJECT (src,"Enumerating devices");
  CGGetOnlineDisplayList (MAX_DISPLAYS,src->m_dispArray,&src->m_displayCount);
  for(CGDirectDisplayID i=0; i<src->m_displayCount; i++) {
    src->m_dispArrayNames[i]=[screenNameForDisplay (src->m_dispArray[i])copy];
    GST_DEBUG_OBJECT (src,"Device ID: %u Name: [%s] ",src->m_dispArray[i],
        [src->m_dispArrayNames[i]UTF8String]);
  }
  OBJC_CALLOUT_END ();
}

static void
gst_osx_screen_cap_src_finalize (GObject*obj)
{
  GstOSXScreenCapSrc*src;
  src=GST_OSX_SCREEN_CAPTURE(obj);
  OBJC_CALLOUT_BEGIN ();
  [GST_OUTPUT_BUFFER (obj)release];
  OBJC_CALLOUT_END ();
  if(src->displayName!=NULL) {
    g_free (src->displayName);
    src->displayName=NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_osx_screen_cap_src_get_property (GObject*object,guint prop_id,
    GValue*value,GParamSpec*pspec)
{
  GstOSXScreenCapSrc*src;
  src=GST_OSX_SCREEN_CAPTURE(object);

  switch(prop_id) {
  case PROP_CAPTURE_SCREEN_CURSOR:
    g_value_set_boolean (value,src->captureScreenCursor);
    break;
  case PROP_CAPTURE_SCREEN_MOUSE_CLICKS:
    g_value_set_boolean (value,src->captureScreenMouseClicks);
    break;
  case PROP_DISPLAY_INDEX:
    g_value_set_int (value,src->displayId);
    break;
  case PROP_DEVICE:
    g_value_set_string (value,src->displayName);
    break;
  case PROP_DO_STATS:
    g_value_set_boolean (value,src->doStats);
    break;
  case PROP_FPS:
    GST_OBJECT_LOCK (object);
    g_value_set_int (value,src->fps);
    GST_OBJECT_UNLOCK (object);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object,prop_id,pspec);
    break;
  }
}

static void
gst_osx_screen_cap_src_set_property (GObject*object,guint prop_id,
    const GValue*value,GParamSpec*pspec)
{
  GstOSXScreenCapSrc*src;
  src=GST_OSX_SCREEN_CAPTURE(object);

  switch(prop_id) {

  case PROP_CAPTURE_SCREEN_CURSOR:
    src->captureScreenCursor=g_value_get_boolean (value);
    break;
  case PROP_CAPTURE_SCREEN_MOUSE_CLICKS:
    src->captureScreenMouseClicks=g_value_get_boolean (value);
    break;

  case PROP_DISPLAY_INDEX:
    src->displayId=g_value_get_int (value);
    break;
  case PROP_DEVICE:
    if(src->displayName!=NULL) {
      g_free (src->displayName);
    }
    src->displayName=g_value_dup_string (value);
    /*
       Find the displayID for the name
     */
    src->displayId=0;
    GST_DEBUG_OBJECT (object,"Search id for given display name: [%s] among %d",
      src->displayName,
      src->m_displayCount);
    for(CGDirectDisplayID i=0; i<src->m_displayCount; i++) {
      if(g_strcmp0( src->displayName,[src->m_dispArrayNames[i]UTF8String])==0) {
        src->displayId=src->m_dispArray[i];
        GST_DEBUG_OBJECT (object,"Selected [%s] id: %u",
            [src->m_dispArrayNames[i]UTF8String],
            src->displayId);
        break;
      }
      ;
    }
    if(src->displayId==0) {
      src->displayId=kCGDirectMainDisplay;
      GST_DEBUG_OBJECT (object,"Not found [%s] using default %u instead",
          src->displayName,
          src->displayId);
    }
    break;
  case PROP_DO_STATS:
    src->doStats=g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object,prop_id,pspec);
    break;
  }
}

static GstStateChangeReturn
gst_osx_screen_cap_src_change_state (GstElement*    element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (element)changeState:transition];
  OBJC_CALLOUT_END ();
  return ret;
}

static GstCaps*
gst_osx_screen_cap_src_get_caps (GstBaseSrc*basesrc)
{
  GstCaps*ret;
  GST_LOG_OBJECT (basesrc,"Get Caps");
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (basesrc)getCaps];
  OBJC_CALLOUT_END ();
  return ret;
}

static gboolean
gst_osx_screen_cap_src_set_caps (GstBaseSrc*basesrc,GstCaps*caps)
{
  gboolean ret;
  GST_LOG_OBJECT (basesrc,"Set Caps");
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (basesrc)setCaps:caps];
  OBJC_CALLOUT_END ();
  return ret;
}

static gboolean
gst_osx_screen_cap_src_start (GstBaseSrc*basesrc)
{
  gboolean ret;
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (basesrc)start];
  OBJC_CALLOUT_END ();
  return ret;
}

static gboolean
gst_osx_screen_cap_src_stop (GstBaseSrc*basesrc)
{
  gboolean ret;
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (basesrc)stop];
  OBJC_CALLOUT_END ();
  return ret;
}

static gboolean
gst_osx_screen_cap_src_query (GstBaseSrc*basesrc,GstQuery*query)
{
  gboolean ret;
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (basesrc)query:query];
  OBJC_CALLOUT_END ();
  return ret;
}

static gboolean
gst_osx_screen_cap_src_unlock (GstBaseSrc*basesrc)
{
  gboolean ret;
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (basesrc)unlock];
  OBJC_CALLOUT_END ();
  return ret;
}

static gboolean
gst_osx_screen_cap_src_unlock_stop (GstBaseSrc*basesrc)
{
  gboolean ret;
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (basesrc)unlockStop];
  OBJC_CALLOUT_END ();
  return ret;
}

static GstFlowReturn
gst_osx_screen_cap_src_create (GstPushSrc*pushsrc,GstBuffer**buf)
{
  GstFlowReturn ret;
  OBJC_CALLOUT_BEGIN ();
  ret=[GST_OUTPUT_BUFFER (pushsrc)create:buf];
  OBJC_CALLOUT_END ();
  return ret;
}

static const GList*
probe_get_properties (GstPropertyProbe*probe)
{
  GObjectClass*klass=G_OBJECT_GET_CLASS (probe);
  static GList*list=NULL;
  GST_CLASS_LOCK (GST_OBJECT_CLASS (klass) );
  if(!list) {
    GParamSpec*pspec;
    pspec=g_object_class_find_property (klass,"device");
    list=g_list_append (NULL,pspec);
  }
  GST_CLASS_UNLOCK (GST_OBJECT_CLASS (klass) );
  return list;
}

static void
probe_probe_property (GstPropertyProbe*probe,guint prop_id,
    const GParamSpec*pspec)
{
  /* we do nothing in here.  the actual "probe" occurs in get_values(),
   * which is a common practice when not caching responses.
   */

  if(!g_str_equal (pspec->name,"device") ) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (probe,prop_id,pspec);
  }
}

static gboolean
probe_needs_probe (GstPropertyProbe*probe,guint prop_id,
    const GParamSpec*pspec)
{
  /* don't cache probed data */
  return TRUE;
}

static GValueArray*
probe_get_values (GstPropertyProbe*probe,guint prop_id,
    const GParamSpec*pspec)
{
  GValueArray*array;
  GValue value={0,};
  GstOSXScreenCapSrc*src;
  src=GST_OSX_SCREEN_CAPTURE(probe);
  if(!g_str_equal (pspec->name,"device") ) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (probe,prop_id,pspec);
    return NULL;
  }

  array=g_value_array_new (src->m_displayCount);
  g_value_init (&value,G_TYPE_STRING);
  for(CGDirectDisplayID i=0; i<src->m_displayCount; i++) {
    g_value_set_string (&value,[src->m_dispArrayNames[i]UTF8String]);
    g_value_array_append (array,&value);
  }
  g_value_unset (&value);
  return array;
}

static void
gst_osx_screen_cap_src_property_probe_interface_init (GstPropertyProbeInterface
    *iface)
{
  iface->get_properties=probe_get_properties;
  iface->probe_property=probe_probe_property;
  iface->needs_probe=probe_needs_probe;
  iface->get_values=probe_get_values;
}

void
gst_osx_screen_cap_src_type_add_device_property_probe_interface (GType type)
{
  static const GInterfaceInfo probe_iface_info={
    (GInterfaceInitFunc)gst_osx_screen_cap_src_property_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,GST_TYPE_PROPERTY_PROBE,
      &probe_iface_info);
}
