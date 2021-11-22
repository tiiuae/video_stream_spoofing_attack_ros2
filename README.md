# Video Stream Spoofing Attack demo

This is a demonstrater for [tiiuae/depthai_ctrl](https://github.com/tiiuae/depthai_ctrl) to demonstrate how to spoof a video stream from a real camera. The streamer node will automatically stop the camera_node and creating a new publisher with a new video stream. The spoofed video stream generated with Gstreamer testvideosrc plugin and can be changed to any inputs. 

### Process steps during the node runtime
 - Send 'stop' command to Gstreamer node with ROS2 message
 - Find camera_node process id and send SIGSTOP to it
 - Send 'start' command to Gstreamer node and sending h264 encoded messages through ROS2 topic
 - Send 'start' command to Gstreamer node with ROS2 message

When the node is stopped, SIGCONT and SIGINT signals will be sent to the camera_node process. The Gstreamer node will go into the fallback state since there are no publishers available for the video stream. It will revert to normal when the camera_node is restarted, either by user command or systemd service control.

## Dependencies and setup

This node requires gstreamer1.0 and gstreamer-plugins-base1.0 packages to be installed. The following command will install required packages (some might not be used):
```
sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-doc gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
```

Build with in your ROS2 workspace:
```
colcon build --symlink-install
```

Run with;
```
ros2 run video_stream_spoofing_attack streamer --ros-args --remap __ns:=/${DRONE_DEVICE_ID} 
```

The namespace will be used for getting drone device id and topic names for sending command messages and publishing video stream.

## Normal stream pipeline

  ```
                                                                                                        
 +-------------+                              +-------------+                       
 |             |                              |             |                       
 |             |       +--------------+       |             |                       
 |             |       |  Compressed  |       |  GStreamer  |                       
 | Camera Node |------>| video stream |------>|    Node     |----------->RTSP SERVER
 |             |       |    topic     |       |             |                       
 |             |       |              |       |             |                       
 |             |       +--------------+       |             |                       
 |             |                              |             |                       
 +-------------+                              +-------------+                       
      
 ```

## Video stream spoofing attack pipeline
```
 +-------------+                              +-------------+                       
 |             |                              |             |                       
 |             |       +--------------+       |             |                       
 |             |       |  Compressed  |       |  GStreamer  |                       
 | Camera Node |       | video stream |------>|    Node     |----------->RTSP SERVER
 |             |       |    topic     |       |             |                       
 |  (STOPPED)  |       |              |       |             |                       
 |             |       +--------------+       |             |                       
 |             |               ^              |             |                       
 +-------------+               |              +-------------+                       
 +-------------+               |                                                    
 |             |               |                                                    
 |             |               |                                              
 | Video Stream|               |                                                    
 |   Spoofing  |----------------                                                    
 |    Node     |                                                                    
 |             |                                                                    
 |             |                                                                    
 |             |                                                                    
 +-------------+                                                                    
  ```


Based on
[makepluscode/ros2-gst-test](https://github.com/makepluscode/ros2-gst-test/)