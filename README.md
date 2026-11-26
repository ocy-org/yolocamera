# YOLO物体检测
  本项目围绕YOLO进行物体检测，核心是QT+ffmpeg+opncv+opengl+YOLO。ffmpeg负责实时读取音视频，opencv以DNN方式调用YOLO算法模型，返回检测的框体BOX和物体类型，opengl把位置和中文标签绘制到屏幕上。
# 效果图
<img width="690" height="539" alt="image" src="https://github.com/user-attachments/assets/7403cc29-87a6-4d84-88c8-4cb09ba58b42" />
# 环境
## YOLO
  YOLO是在python环境下，把yolov8n.pt转成通用的yolov8n.onnx，环境安装及导出过程见以下博文
  https://blog.csdn.net/liangyuna8787/article/details/171308983?spm=1001.2014.3001.5502
  opencv可以通过cv::dnn::readNetFromONNX导入yolov8n.onnx，以下博文是一个简单的读取图片案例
  https://blog.csdn.net/liangyuna8787/article/details/171311143?spm=1001.2014.3001.5502
## opencv
  本项目是在win10 64位，MinGW版本的QT项目，opencv是MinGW编译的，为了跨平台，编译过程见以下博文
  https://blog.csdn.net/liangyuna8787/article/details/149693144?spm=1001.2014.3001.5502
# 功能说明
