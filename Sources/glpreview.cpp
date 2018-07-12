#include <qopengl.h>
#include <cmath>

#include <QPainter>
#include <QLabel>
#include <QOpenGLShaderProgram>
#include <QWindow>

#include "qopenglerrorcheck.h"
#include "glpreview.h"

#define PROGRAM_VERTEX_ATTRIBUTE   0
#define PROGRAM_TEXCOORD_ATTRIBUTE 1

class QPictureLabel : public QLabel
{
private:
    QPixmap _qpSource; //preserve the original, so multiple resize events won't break the quality
    QPixmap _qpCurrent;

    void _displayImage();

public:
    QPictureLabel(QWidget *aParent) : QLabel(aParent) { }
    void setPixmap(QPixmap aPicture);
    void paintEvent(QPaintEvent *aEvent);
};

void QPictureLabel::paintEvent(QPaintEvent *aEvent)
{
    QLabel::paintEvent(aEvent);
    _displayImage();
}

void QPictureLabel::setPixmap(QPixmap aPicture)
{
    _qpSource = _qpCurrent = aPicture;
    update();
}

void QPictureLabel::_displayImage()
{
    if (_qpSource.isNull()) //no image was set, don't draw anything
        return;

    float cw = width(), ch = height();
    float pw = _qpCurrent.width(), ph = _qpCurrent.height();

    if (pw > cw && ph > ch && pw/cw > ph/ch || //both width and high are bigger, ratio at high is bigger or
        pw > cw && ph <= ch || //only the width is bigger or
        pw < cw && ph < ch && cw/pw < ch/ph //both width and height is smaller, ratio at width is smaller
        )
        _qpCurrent = _qpSource.scaledToWidth(cw, Qt::FastTransformation);
    else if (pw > cw && ph > ch && pw/cw <= ph/ch || //both width and high are bigger, ratio at width is bigger or
	     ph > ch && pw <= cw || //only the height is bigger or
	     pw < cw && ph < ch && cw/pw > ch/ph //both width and height is smaller, ratio at height is smaller
	     )
        _qpCurrent = _qpSource.scaledToHeight(ch, Qt::FastTransformation);

    int x = (cw - _qpCurrent.width())/2, y = (ch - _qpCurrent.height())/2;

    QPainter paint(this);
    paint.drawPixmap(x, y, _qpCurrent);
}

/////

GLPreview::GLPreview(QWidget *parent) : QOpenGLWidget(parent), alignType(TextureAll), textures{0}
{
  setObjectName("GLPreview");
  setEnabled(false);

  vbos[2] = QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);

  viewport = QSize(1,1);
}

GLPreview::~GLPreview()
{
  cleanup();
}

void GLPreview::cleanup()
{
    delete program;
}

QSize GLPreview::minimumSizeHint() const
{
    return QSize(360, 120);
}
QSize GLPreview::sizeHint() const
{
    return QSize(500, 120);
}

void GLPreview::initializeGL()
{
    initializeOpenGLFunctions();

    qDebug() << "Loading quad (fragment shader)";
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex, this);
    vshader->compileSourceFile(":/resources/shaders/quad.vert");
    if (!vshader->log().isEmpty()) qDebug() << vshader->log();
    else qDebug() << "done";

    qDebug() << "Loading quad (vertex shader)";
    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment, this);
    fshader->compileSourceFile(":/resources/shaders/quad.frag");
    if (!fshader->log().isEmpty()) qDebug() << fshader->log();
    else qDebug() << "done";

    program = new QOpenGLShaderProgram(this);
    program->addShader(vshader);
    program->addShader(fshader);
    GLCHK( program->bindAttributeLocation("positionIn", PROGRAM_VERTEX_ATTRIBUTE) );
    GLCHK( program->bindAttributeLocation("texCoord", PROGRAM_TEXCOORD_ATTRIBUTE) );
    GLCHK( program->link() );

    GLCHK( program->bind() );

    // make data
    makeScreenQuad();
}

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

void GLPreview::paintGL()
{
    if (!isEnabled()) return;

    GLCHK( glViewport(0, 0, viewport.width(), viewport.height()) );
    GLCHK( glClear(GL_DEPTH_BUFFER_BIT) );

    GLCHK( glDisable(GL_CULL_FACE) );
    GLCHK( glDisable(GL_DEPTH_TEST) );
    GLCHK( glEnable(GL_MULTISAMPLE) );

    GLCHK( glActiveTexture(GL_TEXTURE0) );
    GLCHK( program->bind() );
    GLCHK( vao.bind() );
    for(int t=0; t<MAX_TEXTURES_TYPE; ++t){
      if (textures[t]) {
        GLCHK( glBindTexture(GL_TEXTURE_2D, textures[t]) );
        GLCHK( glDrawElements(GL_TRIANGLES, 3*2, GL_UNSIGNED_INT, BUFFER_OFFSET(t*6*sizeof(uint))) );
      }
    }
    GLCHK( glBindTexture(GL_TEXTURE_2D, 0) );
    GLCHK( vao.release() );
}

void GLPreview::resizeGL(int width, int height)
{
  viewport = QSize(width, height);
  ratio = float(viewport.width())/viewport.height();
}

void GLPreview::textureChanged(TextureTypes imageType, GLuint texture)
{
  qDebug() << "textureChanged/update texture" << imageType;
  textures[imageType] = texture;
  update();
}


static QRect RectScaleTo(const QRect destRect, const QRect srcRect, bool fit) {

    QRect rect = (QRect){0, 0, 0, 0};

    const qreal aspectWidth = qreal(destRect.width()) / qreal(srcRect.width());
    const qreal aspectHeight = qreal(destRect.height()) / qreal(srcRect.height());
    const qreal aspectRatio = fit?qMin( aspectWidth, aspectHeight ):qMax( aspectWidth, aspectHeight );

    rect.setWidth( srcRect.width() * aspectRatio );
    rect.setHeight( srcRect.height() * aspectRatio );

    rect.moveTo( (destRect.width() - rect.width()) / 2, (destRect.height() - rect.height()) / 2 );

    return rect;
}

static QRect RectScaleToFit(const QRect destRect, const QRect srcRect) {
    return RectScaleTo(destRect, srcRect, true);
}

static QRect RectScaleToFill(const QRect destRect, const QRect srcRect) {
    return RectScaleTo(destRect, srcRect, false);
}

void GLPreview::makeScreenQuad()
{
    GLCHK( vao.create() );
    GLCHK( vao.bind() );

    QVector<float> texfull{ 0, 0,
        1, 0,
        1, 1,
        0, 1 };

    QVector<float> vert;
    QVector<float> tex;
    QVector<uint> indices;
    const float boxw = 2.0/MAX_TEXTURES_TYPE; // width of box
    for(int t=0; t< MAX_TEXTURES_TYPE; ++t){
      QVector<float> box{
	  -1+t*boxw, -1, 0,
	  -1+(t+1)*boxw, -1, 0,
	  -1+(t+1)*boxw,  1, 0,
	  -1+t*boxw,  1, 0 };
      vert << box;
      tex << texfull;
      const uint _off = t*4;
      QVector<uint>  quad{
        _off+0,_off+1,_off+2,   // first triangle (bottom left - top left - top right)
        _off+0,_off+2,_off+3 }; // second triangle (bottom left - top right - bottom right)
      indices << quad;
    }

    GLCHK( vbos[0].setUsagePattern( QOpenGLBuffer::StaticDraw ) );
    GLCHK( vbos[0].create() );
    GLCHK( vbos[0].bind() );
    GLCHK( vbos[0].allocate( vert.data(), vert.size() * sizeof( float ) ) );
    GLCHK( program->enableAttributeArray( PROGRAM_VERTEX_ATTRIBUTE ) );
    GLCHK( program->setAttributeBuffer( PROGRAM_VERTEX_ATTRIBUTE, GL_FLOAT, 0, 3 ) );    

    GLCHK( vbos[1].setUsagePattern( QOpenGLBuffer::StaticDraw ) );
    GLCHK( vbos[1].create() );
    GLCHK( vbos[1].bind() );
    GLCHK( vbos[1].allocate( tex.data(), tex.size() * sizeof( float ) ) );
    GLCHK( program->enableAttributeArray( PROGRAM_TEXCOORD_ATTRIBUTE ) );
    GLCHK( program->setAttributeBuffer( PROGRAM_TEXCOORD_ATTRIBUTE, GL_FLOAT, 0, 2 ) );

    GLCHK( vbos[2].setUsagePattern( QOpenGLBuffer::StaticDraw ) );
    GLCHK( vbos[2].create() );
    GLCHK( vbos[2].bind() );
    GLCHK( vbos[2].allocate( indices.data(), indices.size() * sizeof( uint ) ) );

    GLCHK( vao.release() );
}
