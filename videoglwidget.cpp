#include "videoglwidget.h"
#include <QPainter>
#include <QFontMetrics>
#include <QImage>
#include <opencv2/imgproc.hpp>
#include <string.h>
extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#include <QDebug>
// ===== COCO 80类名称表 =====
static const QStringList kClassNames = {
 QStringLiteral("人物"), QStringLiteral("自行车"), QStringLiteral("汽车"),
 QStringLiteral("摩托车"), QStringLiteral("飞机"), QStringLiteral("公交车"),
 QStringLiteral("火车"), QStringLiteral("卡车"), QStringLiteral("船"),
 QStringLiteral("红绿灯"), QStringLiteral("消防栓"), QStringLiteral("停车标志"),
 QStringLiteral("停车计费器"), QStringLiteral("长椅"), QStringLiteral("鸟"),
 QStringLiteral("猫"), QStringLiteral("狗"), QStringLiteral("马"),
 QStringLiteral("羊"), QStringLiteral("牛"), QStringLiteral("大象"),
 QStringLiteral("熊"), QStringLiteral("斑马"), QStringLiteral("长颈鹿"),
 QStringLiteral("背包"), QStringLiteral("雨伞"), QStringLiteral("手提包"),
 QStringLiteral("领带"), QStringLiteral("行李箱"), QStringLiteral("飞盘"),
 QStringLiteral("滑雪板"), QStringLiteral("单板滑雪"), QStringLiteral("运动球"),
 QStringLiteral("风筝"), QStringLiteral("棒球棒"), QStringLiteral("棒球手套"),
 QStringLiteral("滑板"), QStringLiteral("冲浪板"), QStringLiteral("网球拍"),
 QStringLiteral("瓶子"), QStringLiteral("酒杯"), QStringLiteral("杯子"),
 QStringLiteral("叉子"), QStringLiteral("刀"), QStringLiteral("勺子"),
 QStringLiteral("碗"), QStringLiteral("香蕉"), QStringLiteral("苹果"),
 QStringLiteral("三明治"), QStringLiteral("橙子"), QStringLiteral("西兰花"),
 QStringLiteral("胡萝卜"), QStringLiteral("热狗"), QStringLiteral("披萨"),
 QStringLiteral("甜甜圈"), QStringLiteral("蛋糕"), QStringLiteral("椅子"),
 QStringLiteral("沙发"), QStringLiteral("盆栽"), QStringLiteral("床"),
 QStringLiteral("餐桌"), QStringLiteral("马桶"), QStringLiteral("电视"),
 QStringLiteral("笔记本"), QStringLiteral("鼠标"), QStringLiteral("遥控器"),
 QStringLiteral("键盘"), QStringLiteral("手机"), QStringLiteral("微波炉"),
 QStringLiteral("烤箱"), QStringLiteral("烤面包机"), QStringLiteral("水槽"),
 QStringLiteral("冰箱"), QStringLiteral("书"), QStringLiteral("时钟"),
 QStringLiteral("花瓶"), QStringLiteral("剪刀"), QStringLiteral("泰迪熊"),
 QStringLiteral("吹风机"), QStringLiteral("牙刷") };


// 类别→Qt颜色（与你的10色调色板一致，注意BGR→RGB翻转）
static QColor classQColor(int id) {
    static const QColor palette[] = {
        Qt::green, Qt::blue, Qt::red, Qt::yellow, Qt::magenta,
        Qt::cyan, QColor(255,128,0), QColor(255,0,128),
        QColor(0,255,128), QColor(128,255,0) };
    return palette[id % 10];
}

// GLSL 100风格,Qt在桌面端自动把highp/mediump映射为空宏,兼容GL2.1~4.6
static const char* kVS = R"(
attribute highp vec2 aPos;   // 视频像素坐标,CPU每帧生成
attribute highp vec2 aUV;    // 0..1
uniform highp mat3 uMVP;
varying highp vec2 vUV;
void main(){
    gl_Position = vec4((uMVP*vec3(aPos,1.0)).xy, 0.0, 1.0);
    vUV = aUV;
})";

static const char* kFS = R"(
uniform sampler2D texY;
uniform sampler2D texUV;
uniform sampler2D texV;
uniform highp vec2 uVideoSize;
uniform lowp int uFmt;          // 1=YUV420平面 2=YUYV打包
uniform lowp int uFullRange;
uniform lowp int uBt709;
varying highp vec2 vUV;
void main(){
    mediump float y; mediump vec2 ch;
    if(uFmt==2){
        highp float fx = vUV.x*uVideoSize.x;
        highp vec2 tuv = vec2((floor(fx*0.5)+0.5)/(uVideoSize.x*0.5), vUV.y);
        vec4 t = texture2D(texY,tuv);            // R=Y0 G=U B=Y1 A=V
        y  = mix(t.r, t.b, step(1.0, mod(fx,2.0)));
        ch = t.ga - 0.5;                         // YUYV恒为full range
    }else{
        y = texture2D(texY,vUV).r;
        if(uFullRange==0) y = (y*255.0-16.0)/219.0;   // limited range展开
        ch = vec2(texture2D(texUV,vUV).r, texture2D(texV,vUV).r) - 0.5;
    }
    mediump vec3 rgb;
    if(uBt709==1){
        rgb.r = 1.1644*y + 1.7927*ch.y;
        rgb.g = 1.1644*y - 0.2132*ch.x - 0.5329*ch.y;
        rgb.b = 1.1644*y + 2.1124*ch.x;
    }else{
        rgb.r = 1.1644*y + 1.5960*ch.y;
        rgb.g = 1.1644*y - 0.3918*ch.x - 0.8130*ch.y;
        rgb.b = 1.1644*y + 2.0172*ch.x;
    }
    gl_FragColor = vec4(clamp(rgb,0.0,1.0),1.0);
})";
static const char* kLineVS = R"(
attribute highp vec2 aPos;              // 视频像素坐标
uniform highp mat3 uMVP;
void main(){ gl_Position=vec4((uMVP*vec3(aPos,1.0)).xy,0.0,1.0); })";

static const char* kLineFS = R"(
uniform lowp vec4 uColor;
void main(){ gl_FragColor=uColor; })";

// ★ 标签专用 shader：只做一次贴图，坐标是 FBO 物理像素
static const char* kLabelVS = R"(
attribute highp vec2 aPos;
attribute highp vec2 aUV;
uniform highp mat3 uMVP;
varying highp vec2 vUV;
void main(){ gl_Position=vec4((uMVP*vec3(aPos,1.0)).xy,0.0,1.0); vUV=aUV; })";

static const char* kLabelFS = R"(
uniform sampler2D uTex;
varying highp vec2 vUV;
void main(){ gl_FragColor=texture2D(uTex,vUV); })";

VideoGLWidget::VideoGLWidget(QWidget* p):QOpenGLWidget(p)
  ,m_vbo(QOpenGLBuffer::VertexBuffer)
  ,m_lineVbo(QOpenGLBuffer::VertexBuffer)
  ,m_labelVbo(QOpenGLBuffer::VertexBuffer){
    // ★ 关键: QOpenGLWidget不会自动重绘,paintGL是唯一取帧入口,必须有持续的重绘驱动
    m_repaintTimer = new QTimer(this);
    connect(m_repaintTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    m_repaintTimer->start(33);   // ~30fps,与摄像头帧率匹配
}

void VideoGLWidget::clearFrame()
{
    m_cur=FramePacket(); m_texW=0;
    m_contour.clear(); m_manualVisible=false; m_dragVisible=false;
    update();
    m_dets.clear();
}

void VideoGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.07f,0.08f,0.09f,1.f);
    //把 YUV 纹理算成彩色视频画面
    m_prog=new QOpenGLShaderProgram(this);
    m_prog->addShaderFromSourceCode(QOpenGLShader::Vertex,kVS);
    m_prog->addShaderFromSourceCode(QOpenGLShader::Fragment,kFS);
    m_prog->link();
    m_aPos=m_prog->attributeLocation("aPos");
    m_mvpLoc=m_prog->uniformLocation("uMVP");
    //用纯色描出一圈检测绿框
    m_lineProg=new QOpenGLShaderProgram(this);
    m_lineProg->addShaderFromSourceCode(QOpenGLShader::Vertex,kLineVS);
    m_lineProg->addShaderFromSourceCode(QOpenGLShader::Fragment,kLineFS);
    m_lineProg->link();
    m_lineApos=m_lineProg->attributeLocation("aPos");
    m_lineMvpLoc=m_lineProg->uniformLocation("uMVP");

    static const GLfloat quad[]={-0.5f,-0.5f, 0.5f,-0.5f, -0.5f,0.5f, 0.5f,0.5f};//四个顶点
    m_vbo.create(); m_vbo.bind(); m_vbo.allocate(quad,sizeof(quad)); m_vbo.release();
    m_lineVbo.create();

    glGenTextures(1,&m_texY); glGenTextures(1,&m_texU); glGenTextures(1,&m_texV);
    for(GLuint t:{m_texY,m_texU,m_texV}){
        glBindTexture(GL_TEXTURE_2D,t);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }

    // ===== ★ 标签叠加层：独立 shader / VBO / 一张 NEAREST 小纹理 =====
    m_labelProg = new QOpenGLShaderProgram(this);
    m_labelProg->addShaderFromSourceCode(QOpenGLShader::Vertex,   kLabelVS);
    m_labelProg->addShaderFromSourceCode(QOpenGLShader::Fragment, kLabelFS);
    m_labelProg->link();
    m_labelApos    = m_labelProg->attributeLocation("aPos");
    m_labelAuv     = m_labelProg->attributeLocation("aUV");
    m_labelMvpLoc  = m_labelProg->uniformLocation("uMVP");
    m_labelVbo.create();

    glGenTextures(1, &m_labelTex);
    glBindTexture(GL_TEXTURE_2D, m_labelTex);
    // ★ 关键：不做任何插值。字形在 CPU 端已按物理像素栅格化好，1:1 贴上去
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void VideoGLWidget::ensureTexSize()
{
    const int key=(m_cur.fmt==FramePacket::FMT_YUYV422)?2:1;
    if(m_texW==m_cur.width && m_texH==m_cur.height && m_texFmtKey==key) return;
    m_texW=m_cur.width; m_texH=m_cur.height; m_texFmtKey=key;
    if(key==1){   // GL_LUMINANCE兼容GL2.1~4.6;若将来上core profile改GL_R8/GL_RED
        glBindTexture(GL_TEXTURE_2D,m_texY);
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,m_texW,m_texH,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,nullptr);
        glBindTexture(GL_TEXTURE_2D,m_texU);
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,m_texW/2,m_texH/2,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,nullptr);
        glBindTexture(GL_TEXTURE_2D,m_texV);
        glTexImage2D(GL_TEXTURE_2D,0,GL_LUMINANCE,m_texW/2,m_texH/2,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,nullptr);
    }else{
        glBindTexture(GL_TEXTURE_2D,m_texY);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,m_texW/2,m_texH,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    }
}

void VideoGLWidget::uploadFrame(const FramePacket& f)
{
    static int lastW=0, lastH=0;
    if(f.width!=lastW || f.height!=lastH){
        lastW=f.width; lastH=f.height;
        qDebug()<<"[GL] 实际解码帧:"<<f.width<<"x"<<f.height
                <<" fmt="<<f.fmt
                <<" widget="<<width()<<"x"<<height()
                <<" dpr="<<devicePixelRatioF();
    }

    m_cur = f;   // ★ 原来只拷贝了宽高参数,把plane原始数据丢了。整包保存,拍照直接用原始数据
    ensureTexSize();
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if(m_cur.fmt==FramePacket::FMT_YUV420P){
        glBindTexture(GL_TEXTURE_2D,m_texY);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,m_cur.width,m_cur.height,GL_LUMINANCE,GL_UNSIGNED_BYTE,m_cur.plane[0].constData());
        const int cw=m_cur.width/2, ch=m_cur.height/2;
        glBindTexture(GL_TEXTURE_2D,m_texU);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,cw,ch,GL_LUMINANCE,GL_UNSIGNED_BYTE,m_cur.plane[1].constData());
        glBindTexture(GL_TEXTURE_2D,m_texV);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,cw,ch,GL_LUMINANCE,GL_UNSIGNED_BYTE,m_cur.plane[2].constData());
    }else{
        glBindTexture(GL_TEXTURE_2D,m_texY);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,m_cur.width/2,m_cur.height,GL_RGBA,GL_UNSIGNED_BYTE,m_cur.plane[0].constData());
    }
}


// 视频像素→旋转→等比缩放居中→clip。m_a同时保存"视频→窗口"仿射,供鼠标反算。
void VideoGLWidget::updateXform()
{
    const double winW=qMax(1,width()), winH=qMax(1,height());
    const bool swap=(m_rot==90||m_rot==270);
    const double cw=swap?m_cur.height:m_cur.width;
    const double ch=swap?m_cur.width:m_cur.height;
    if(cw<1||ch<1) return;
    const double s=qMin(winW*m_zoom/cw, winH*m_zoom/ch);       // KeepAspectRatio,只算一次
    const double ox=(winW-s*cw)/2.0, oy=(winH-s*ch)/2.0;

    double r00=1,r01=0,r02=0,r10=0,r11=1,r12=0;                // y向下坐标系,顺时针
    switch(m_rot){
    case 90:  r00=0; r01=-1; r02=m_cur.height; r10=1;  r11=0;  r12=0;            break;
    case 180: r00=-1;r01=0;  r02=m_cur.width;  r10=0;  r11=-1; r12=m_cur.height; break;
    case 270: r00=0; r01=1;  r02=0;            r10=-1; r11=0;  r12=m_cur.width;  break;
    }
    m_a[0]=s*r00; m_a[1]=s*r01; m_a[2]=s*r02+ox;
    m_a[3]=s*r10; m_a[4]=s*r11; m_a[5]=s*r12+oy;
    const double c0=2.0/winW, c1=-2.0/winH;                    // 窗口像素→clip(y翻转)
    m_mvp[0]=GLfloat(c0*m_a[0]); m_mvp[1]=GLfloat(c1*m_a[3]); m_mvp[2]=0;
    m_mvp[3]=GLfloat(c0*m_a[1]); m_mvp[4]=GLfloat(c1*m_a[4]); m_mvp[5]=0;
    m_mvp[6]=GLfloat(c0*m_a[2]-1); m_mvp[7]=GLfloat(c1*m_a[5]+1); m_mvp[8]=1;
}

void VideoGLWidget::drawVideo(const GLfloat* mvp9)
{
    // CPU生成视频像素坐标quad: 与updateXform同一数据源(m_cur)
    const GLfloat w = GLfloat(m_cur.width), h = GLfloat(m_cur.height);
    const GLfloat quad[16] = { 0,0, 0,0,   w,0, 1,0,   0,h, 0,1,   w,h, 1,1 };

    m_prog->bind();
    m_vbo.bind();
    m_vbo.allocate(quad, sizeof(quad));
    m_prog->enableAttributeArray(m_aPos);
    m_prog->setAttributeBuffer(m_aPos, GL_FLOAT, 0, 2, 4*sizeof(GLfloat));
    const int aUV = m_prog->attributeLocation("aUV");
    m_prog->enableAttributeArray(aUV);
    m_prog->setAttributeBuffer(aUV, GL_FLOAT, 2*sizeof(GLfloat), 2, 4*sizeof(GLfloat));

    // uVideoSize 只剩fragment里YUYV分支在用,几何已不依赖它
    m_prog->setUniformValue("uVideoSize", QVector2D(float(m_cur.width), float(m_cur.height)));
    m_prog->setUniformValue("uFmt", m_cur.fmt);
    m_prog->setUniformValue("uFullRange", m_cur.fullRange?1:0);
    m_prog->setUniformValue("uBt709", m_bt709?1:0);
    glUniformMatrix3fv(m_mvpLoc, 1, GL_FALSE, mvp9);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_texY); m_prog->setUniformValue("texY", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_cur.fmt==2?m_texY:m_texU); m_prog->setUniformValue("texUV", 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m_cur.fmt==2?m_texY:m_texV); m_prog->setUniformValue("texV", 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_prog->disableAttributeArray(m_aPos);
    m_prog->disableAttributeArray(aUV);
    m_vbo.release(); m_prog->release();
}


void VideoGLWidget::drawContour()
{
    if(m_contour.size()!=4 || m_cur.width==0) return;
    GLfloat pts[8];
    for(int i=0;i<4;++i){ pts[2*i]=float(m_contour[i].x()); pts[2*i+1]=float(m_contour[i].y()); }
    m_lineVbo.bind(); m_lineVbo.allocate(pts,sizeof(pts));
    m_lineProg->bind();
    m_lineProg->enableAttributeArray(m_lineApos);
    m_lineProg->setAttributeBuffer(m_lineApos,GL_FLOAT,0,2,0);
    glUniformMatrix3fv(m_lineMvpLoc,1,GL_FALSE,m_mvp);   // 与视频同一矩阵→永远贴合
    m_lineProg->setUniformValue("uColor",QVector4D(0.f,1.f,0.f,1.f));
    glLineWidth(2.0f);
    glDrawArrays(GL_LINE_LOOP,0,4);
    m_lineProg->disableAttributeArray(m_lineApos);
    m_lineVbo.release(); m_lineProg->release();
}

void VideoGLWidget::drawDets()
{
    if(m_dets.isEmpty() || m_cur.width<=0) return;
    m_lineVbo.bind();
    m_lineProg->bind();
    m_lineProg->enableAttributeArray(m_lineApos);
    m_lineProg->setAttributeBuffer(m_lineApos,GL_FLOAT,0,2,0);
    glUniformMatrix3fv(m_lineMvpLoc,1,GL_FALSE,m_mvp);
    glLineWidth(2.0f);
    for(const auto& it : m_dets){
        const QColor c = classQColor(it.cls);
        m_lineProg->setUniformValue("uColor",
            QVector4D(c.redF(), c.greenF(), c.blueF(), 1.f));
        const GLfloat x0=GLfloat(it.box.left()),  y0=GLfloat(it.box.top());
        const GLfloat x1=GLfloat(it.box.right()), y1=GLfloat(it.box.bottom());
        const GLfloat pts[8]={ x0,y0, x1,y0, x1,y1, x0,y1 };
        m_lineVbo.allocate(pts,sizeof(pts));
        glDrawArrays(GL_LINE_LOOP,0,4);
    }
    m_lineProg->disableAttributeArray(m_lineApos);
    m_lineVbo.release();
    m_lineProg->release();
}


// ===== ★ YOLO 标签：CPU 按物理像素栅格化成小图 → GL_NEAREST 1:1 贴到 FBO =====
// 为什么不用 QPainter(this).drawText：
//   QOpenGLWidget 上的 QPainter 走 OpenGL glyph cache，字形按逻辑坐标栅格化后
//   再被放大到物理 FBO，且 QRectF 小数坐标触发双线性插值 → 字发虚、边缘出彩边。
//   这里全程整数物理像素 + NEAREST，没有任何第二次采样。
void VideoGLWidget::drawLabelsGL()
{
    if(m_dets.isEmpty() || m_cur.width <= 0 || !m_labelProg || !m_labelTex)
        return;

    const qreal dpr  = devicePixelRatioF();
    const int   winW = qMax(1, int(width()  * dpr));   // FBO 物理尺寸
    const int   winH = qMax(1, int(height() * dpr));

    // 物理像素字号：dpr=1 约 14px，dpr=2 为 28px，屏幕视觉大小保持一致
    QFont f;
    f.setPixelSize(qMax(10, qRound(14 * dpr)));
    f.setBold(true);
    const QFontMetrics fm(f);

    // 物理像素 → clip（y 翻转，与 updateXform 的 m_mvp 同一约定）
    const GLfloat mvp[9] = {
        GLfloat( 2.0/winW), 0,                  0,
        0,                  GLfloat(-2.0/winH), 0,
        -1,                 1,                  1 };

    m_labelVbo.bind();
    m_labelProg->bind();
    m_labelProg->enableAttributeArray(m_labelApos);
    m_labelProg->setAttributeBuffer(m_labelApos, GL_FLOAT, 0, 2, 4*sizeof(GLfloat));
    m_labelProg->enableAttributeArray(m_labelAuv);
    m_labelProg->setAttributeBuffer(m_labelAuv,  GL_FLOAT, 2*sizeof(GLfloat), 2, 4*sizeof(GLfloat));
    glUniformMatrix3fv(m_labelMvpLoc, 1, GL_FALSE, mvp);
    m_labelProg->setUniformValue("uTex", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_labelTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for(const auto& it : m_dets){
        if(it.cls < 0 || it.cls >= 80) continue;

        const QString label = QStringLiteral("%1 %2%")
            .arg(kClassNames.at(it.cls))
            .arg(qRound(it.conf * 100));

        const int tw = fm.horizontalAdvance(label);   // Qt<5.11 改为 fm.width(label)
        const int th = fm.height();
        const int bw = tw + 10, bh = th + 6;

        // m_a 输出的是逻辑窗口坐标 → ×dpr 换成物理像素
        const int vx = qRound((m_a[0]*it.box.left() + m_a[1]*it.box.top() + m_a[2]) * dpr);
        const int vy = qRound((m_a[3]*it.box.left() + m_a[4]*it.box.top() + m_a[5]) * dpr);
        const int maxX = qMax(2, winW - bw - 2);
        const int maxY = qMax(2, winH - bh - 2);
        int bx = qBound(2, vx, maxX);
        int by = vy - bh - 2;
        if(by < 2) by = vy + 2;          // 框贴顶时，标签改画在框内侧，避免被裁掉
        by = qBound(2, by, maxY);

        // 只做标签这一小块，不做全屏 overlay —— 全屏透明图贴回去会盖黑视频
        QImage img(bw, bh, QImage::Format_ARGB32_Premultiplied);  // Qt 光栅引擎原生格式
        img.fill(QColor(0, 0, 0, 235));                            // 接近不透明，白字不"肉"
        QPainter ip(&img);
        ip.setRenderHint(QPainter::TextAntialiasing, true);
        ip.setFont(f);
        ip.setPen(QPen(classQColor(it.cls), 1));
        ip.drawRect(QRect(0, 0, bw - 1, bh - 1));
        ip.setPen(Qt::white);
        ip.drawText(QRect(5, 3, tw, th), Qt::AlignLeft | Qt::AlignVCenter, label);
        ip.end();
        // 转成 GL 要的 R,G,B,A 字节序（同时反预乘，配合 SRC_ALPHA 混合）
        const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bw, bh, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());

        // 整数物理像素 quad。y0 是屏幕上方，QImage 第 0 行也是图顶，
        // glTexImage2D 的数据首行对应 v=0，所以这里 UV 不需要翻转。
        const GLfloat x0=GLfloat(bx),     y0=GLfloat(by);
        const GLfloat x1=GLfloat(bx+bw),  y1=GLfloat(by+bh);
        const GLfloat quad[16] = {
            x0,y0, 0,0,   x1,y0, 1,0,   x0,y1, 0,1,   x1,y1, 1,1 };

        m_labelVbo.allocate(quad, sizeof(quad));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisable(GL_BLEND);
    m_labelProg->disableAttributeArray(m_labelApos);
    m_labelProg->disableAttributeArray(m_labelAuv);
    m_labelVbo.release();
    m_labelProg->release();
}


void VideoGLWidget::paintGL()
{
    // ★ QPainter会改写viewport为逻辑尺寸,而FBO是物理尺寸(逻辑×DPR)。
    //   每帧原生GL绘制前必须恢复为整个FBO,否则画面被压进左上角。
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, int(width()*dpr), int(height()*dpr));
    // ★ QPainter不保证恢复其改动的GL状态,这些是我们纹理上传/绘制依赖的
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_SCISSOR_TEST);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glClear(GL_COLOR_BUFFER_BIT);
    FramePacket pkt;
    // 从邮箱取最新帧 → 上传纹理 → 算矩阵
    if(m_mail && m_mail->fetch(pkt)){
        uploadFrame(pkt); //把 YUV 数据上传成 GPU 纹理
        updateXform();
    }
    if(m_cur.width>0) drawVideo(m_mvp);//绑定着色器、绑定纹理
    drawContour();
    drawDets();
    drawLabelsGL();     // ★ 必须放在 QPainter 之前：此处 GL 状态是干净的
    if(m_cur.width == 0){
        // ★占位文字在GL控件内实现:先栅格化到QImage(CPU光栅渲染,和QLabel同一引擎,清晰),
        //   再整图1:1贴上。只在无帧分支出现,物理上不可能遮挡视频画面。
        const qreal dpr = devicePixelRatioF();
        const int iw = qMax(1,int(width()*dpr)), ih = qMax(1,int(height()*dpr));
        if(m_phImg.isNull() || m_phImg.width()!=iw || m_phImg.height()!=ih){
            m_phImg = QImage(iw,ih,QImage::Format_RGBA8888_Premultiplied);
            m_phImg.setDevicePixelRatio(dpr);
            m_phImg.fill(Qt::transparent);
            QPainter ip(&m_phImg);
            ip.setRenderHint(QPainter::TextAntialiasing,true);
            QFont f=ip.font(); f.setPointSize(16); f.setBold(true);
            ip.setFont(f);
            ip.setPen(QColor(235,235,235));
            ip.drawText(QRect(0,0,width(),height()),Qt::AlignCenter,
                        QStringLiteral("点击\"打开摄像头\"开始预览"));
            ip.end();
        }
        QPainter p(this);
        p.fillRect(rect(),QColor(18,18,18));      // 黑底(和原GL清屏色一致)
        p.drawImage(rect(),m_phImg);              // 1:1物理像素贴图,不糊
        p.end();
    }else{
        QPainter p(this);                                     // QPainter叠加层(只画矩形)
        p.setRenderHint(QPainter::Antialiasing);
        if(m_manualVisible && m_manualRect.width()>1){        // 视频坐标→窗口坐标
            const QPointF a=QPointF(m_a[0]*m_manualRect.left()+m_a[1]*m_manualRect.top()+m_a[2],
                                    m_a[3]*m_manualRect.left()+m_a[4]*m_manualRect.top()+m_a[5]);
            const QPointF b=QPointF(m_a[0]*m_manualRect.right()+m_a[1]*m_manualRect.bottom()+m_a[2],
                                    m_a[3]*m_manualRect.right()+m_a[4]*m_manualRect.bottom()+m_a[5]);
            p.setPen(QPen(Qt::red,2)); p.setBrush(QColor(255,0,0,30));
            p.drawRect(QRectF(a,b).normalized());
        }
        if(m_dragVisible){
            p.setPen(QPen(Qt::yellow,2,Qt::DashLine)); p.setBrush(QColor(255,255,0,50));
            p.drawRect(m_dragRect);
        }
        // ★ 原来的 QPainter 标签绘制整段已删除：标签改由 drawLabelsGL() 画，
        //   这里只保留手动框/拖拽框，避免两层重复绘制把糊的那层盖回来。
        p.end();
    }
}

QPointF VideoGLWidget::widgetToVideo(const QPointF& p) const
{
    const double det=m_a[0]*m_a[4]-m_a[1]*m_a[3];
    if(qFuzzyIsNull(det)) return QPointF();
    const double dx=p.x()-m_a[2], dy=p.y()-m_a[5];
    return QPointF(( m_a[4]*dx-m_a[1]*dy)/det,
                   (-m_a[3]*dx+m_a[0]*dy)/det);
}

// ===== YUV→BGR整帧CPU转换(sws,无任何缩放,和旧版latestHighResFrame完全同源) =====
static cv::Mat packetToBgr(const FramePacket& f)
{
    if(f.width<=0 || f.height<=0) return cv::Mat();
    AVPixelFormat src = AV_PIX_FMT_NONE;
    int stride[3] = { f.width, f.width/2, f.width/2 };
    if(f.fmt==FramePacket::FMT_YUV420P){
        src = AV_PIX_FMT_YUV420P;
    }else if(f.fmt==FramePacket::FMT_YUYV422){
        src = AV_PIX_FMT_YUYV422;
        stride[0] = f.width*2;      // 打包格式,单平面
    }else{
        return cv::Mat();
    }

    // 仅UI线程调用,静态复用;尺寸/格式变化时自动重建
    static SwsContext* sws = nullptr;
    static int lastW=0, lastH=0;
    static AVPixelFormat lastFmt = AV_PIX_FMT_NONE;
    if(!sws || lastW!=f.width || lastH!=f.height || lastFmt!=src){
        sws_freeContext(sws);
        sws = sws_getContext(f.width,f.height,src,
                             f.width,f.height,AV_PIX_FMT_BGR24,
                             SWS_BICUBIC,nullptr,nullptr,nullptr);
        lastW=f.width; lastH=f.height; lastFmt=src;
    }
    if(!sws) return cv::Mat();

    cv::Mat bgr(f.height, f.width, CV_8UC3);
    const uint8_t* srcPlanes[3] = {
        (const uint8_t*)f.plane[0].constData(),
        (const uint8_t*)f.plane[1].constData(),
        (const uint8_t*)f.plane[2].constData() };
    uint8_t* dst[1]      = { bgr.data };
    int      dstStride[1]= { int(bgr.step) };
    sws_scale(sws, srcPlanes, stride, 0, f.height, dst, dstStride);
    return bgr;
}

cv::Mat VideoGLWidget::grabFrameMat(bool rotated)
{
    // ★ 拍照不经过GPU:直接用摄像头原始帧整帧转换。
    //   选中什么分辨率,拍出来就是什么分辨率;裁剪也是从这张全分辨率原图上裁。
    if(m_cur.width<=0 || m_cur.height<=0) return cv::Mat();
    qDebug() << "[Capture] 原图帧:" << m_cur.width << "x" << m_cur.height;
    cv::Mat bgr = packetToBgr(m_cur);
    if(bgr.empty()) return cv::Mat();
    if(rotated){
        const int r = ((m_rot%360)+360)%360;
        if(r==90)  cv::rotate(bgr,bgr,cv::ROTATE_90_CLOCKWISE);
        if(r==180) cv::rotate(bgr,bgr,cv::ROTATE_180);
        if(r==270) cv::rotate(bgr,bgr,cv::ROTATE_90_COUNTERCLOCKWISE);
    }
    return bgr;   // BGR24、全分辨率,与旧版latestHighResFrame同构
}
