/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include "qsvgiconengine.h"

#ifndef QT_NO_SVGRENDERER

#include "qpainter.h"
#include "qpixmap.h"
#include "qsvgrenderer.h"
#include "qpixmapcache.h"
#include "qfileinfo.h"
#include <QAtomicInt>
#include "qdebug.h"
#include <private/qguiapplication_p.h>

QT_BEGIN_NAMESPACE

class QSvgIconEnginePrivate : public QSharedData
{
public:
    QSvgIconEnginePrivate()
        : svgBuffers(0), addedPixmaps(0)
        { stepSerialNum(); }

    ~QSvgIconEnginePrivate()
        { delete addedPixmaps; delete svgBuffers; }

    static int hashKey(QIcon::Mode mode, QIcon::State state)
        { return (((mode)<<4)|state); }

    QString pmcKey(const QSize &size, QIcon::Mode mode, QIcon::State state)
        { return QLatin1String("$qt_svgicon_")
                 + QString::number(serialNum, 16).append(QLatin1Char('_'))
                 + QString::number((((((size.width()<<11)|size.height())<<11)|mode)<<4)|state, 16); }

    void stepSerialNum()
        { serialNum = lastSerialNum.fetchAndAddRelaxed(1); }

    void loadDataForModeAndState(QSvgRenderer *renderer, QIcon::Mode mode, QIcon::State state);

    QHash<int, QString> svgFiles;
    QHash<int, QByteArray> *svgBuffers;
    QHash<int, QPixmap> *addedPixmaps;
    int serialNum;
    static QAtomicInt lastSerialNum;
};

QAtomicInt QSvgIconEnginePrivate::lastSerialNum;

QSvgIconEngine::QSvgIconEngine()
    : d(new QSvgIconEnginePrivate)
{
}

QSvgIconEngine::QSvgIconEngine(const QSvgIconEngine &other)
    : QIconEngine(other), d(new QSvgIconEnginePrivate)
{
    d->svgFiles = other.d->svgFiles;
    if (other.d->svgBuffers)
        d->svgBuffers = new QHash<int, QByteArray>(*other.d->svgBuffers);
    if (other.d->addedPixmaps)
        d->addedPixmaps = new QHash<int, QPixmap>(*other.d->addedPixmaps);
}


QSvgIconEngine::~QSvgIconEngine()
{
}


QSize QSvgIconEngine::actualSize(const QSize &size, QIcon::Mode mode,
                                 QIcon::State state)
{
    if (d->addedPixmaps) {
        QPixmap pm = d->addedPixmaps->value(d->hashKey(mode, state));
        if (!pm.isNull() && pm.size() == size)
            return size;
    }

    QPixmap pm = pixmap(size, mode, state);
    if (pm.isNull())
        return QSize();
    return pm.size();
}

void QSvgIconEnginePrivate::loadDataForModeAndState(QSvgRenderer *renderer, QIcon::Mode mode, QIcon::State state)
{
    QByteArray buf;
    if (svgBuffers) {
        buf = svgBuffers->value(hashKey(mode, state));
        if (buf.isEmpty())
            buf = svgBuffers->value(hashKey(QIcon::Normal, QIcon::Off));
    }
    if (!buf.isEmpty()) {
#ifndef QT_NO_COMPRESS
        buf = qUncompress(buf);
#endif
        renderer->load(buf);
    } else {
        QString svgFile = svgFiles.value(hashKey(mode, state));
        if (svgFile.isEmpty())
            svgFile = svgFiles.value(hashKey(QIcon::Normal, QIcon::Off));
        if (!svgFile.isEmpty())
            renderer->load(svgFile);
    }
}

QPixmap QSvgIconEngine::pixmap(const QSize &size, QIcon::Mode mode,
                               QIcon::State state)
{
    QPixmap pm;

    QString pmckey(d->pmcKey(size, mode, state));
    if (QPixmapCache::find(pmckey, pm))
        return pm;

    if (d->addedPixmaps) {
        pm = d->addedPixmaps->value(d->hashKey(mode, state));
        if (!pm.isNull() && pm.size() == size)
            return pm;
    }

    QSvgRenderer renderer;
    d->loadDataForModeAndState(&renderer, mode, state);
    if (!renderer.isValid())
        return pm;

    QSize actualSize = renderer.defaultSize();
    if (!actualSize.isNull())
        actualSize.scale(size, Qt::KeepAspectRatio);

    if (actualSize.isEmpty())
        return QPixmap();

    QImage img(actualSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(0x00000000);
    QPainter p(&img);
    renderer.render(&p);
    p.end();
    pm = QPixmap::fromImage(img);
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        const QPixmap generated = QGuiApplicationPrivate::instance()->applyQIconStyleHelper(mode, pm);
        if (!generated.isNull())
            pm = generated;
    }

    if (!pm.isNull())
        QPixmapCache::insert(pmckey, pm);

    return pm;
}


void QSvgIconEngine::addPixmap(const QPixmap &pixmap, QIcon::Mode mode,
                               QIcon::State state)
{
    if (!d->addedPixmaps)
        d->addedPixmaps = new QHash<int, QPixmap>;
    d->stepSerialNum();
    d->addedPixmaps->insert(d->hashKey(mode, state), pixmap);
}


void QSvgIconEngine::addFile(const QString &fileName, const QSize &,
                             QIcon::Mode mode, QIcon::State state)
{
    if (!fileName.isEmpty()) {
        QString abs = fileName;
        if (fileName.at(0) != QLatin1Char(':'))
            abs = QFileInfo(fileName).absoluteFilePath();
        if (abs.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
#ifndef QT_NO_COMPRESS
                || abs.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive)
                || abs.endsWith(QLatin1String(".svg.gz"), Qt::CaseInsensitive))
#endif
        {
            QSvgRenderer renderer(abs);
            if (renderer.isValid()) {
                d->stepSerialNum();
                d->svgFiles.insert(d->hashKey(mode, state), abs);
            }
        } else {
            QPixmap pm(abs);
            if (!pm.isNull())
                addPixmap(pm, mode, state);
        }
    }
}

void QSvgIconEngine::paint(QPainter *painter, const QRect &rect,
                           QIcon::Mode mode, QIcon::State state)
{
    painter->drawPixmap(rect, pixmap(rect.size(), mode, state));
}

QString QSvgIconEngine::key() const
{
    return QLatin1String("svg");
}

QIconEngine *QSvgIconEngine::clone() const
{
    return new QSvgIconEngine(*this);
}


bool QSvgIconEngine::read(QDataStream &in)
{
    d = new QSvgIconEnginePrivate;
    d->svgBuffers = new QHash<int, QByteArray>;

    if (in.version() >= QDataStream::Qt_4_4) {
        int isCompressed;
        QHash<int, QString> fileNames;  // For memoryoptimization later
        in >> fileNames >> isCompressed >> *d->svgBuffers;
#ifndef QT_NO_COMPRESS
        if (!isCompressed) {
            foreach(int key, d->svgBuffers->keys())
                d->svgBuffers->insert(key, qCompress(d->svgBuffers->value(key)));
        }
#else
        if (isCompressed) {
            qWarning("QSvgIconEngine: Can not decompress SVG data");
            d->svgBuffers->clear();
        }
#endif
        int hasAddedPixmaps;
        in >> hasAddedPixmaps;
        if (hasAddedPixmaps) {
            d->addedPixmaps = new QHash<int, QPixmap>;
            in >> *d->addedPixmaps;
        }
    }
    else {
        QPixmap pixmap;
        QByteArray data;
        uint mode;
        uint state;
        int num_entries;

        in >> data;
        if (!data.isEmpty()) {
#ifndef QT_NO_COMPRESS
            data = qUncompress(data);
#endif
            if (!data.isEmpty())
                d->svgBuffers->insert(d->hashKey(QIcon::Normal, QIcon::Off), data);
        }
        in >> num_entries;
        for (int i=0; i<num_entries; ++i) {
            if (in.atEnd())
                return false;
            in >> pixmap;
            in >> mode;
            in >> state;
            // The pm list written by 4.3 is buggy and/or useless, so ignore.
            //addPixmap(pixmap, QIcon::Mode(mode), QIcon::State(state));
        }
    }

    return true;
}


bool QSvgIconEngine::write(QDataStream &out) const
{
    if (out.version() >= QDataStream::Qt_4_4) {
        int isCompressed = 0;
#ifndef QT_NO_COMPRESS
        isCompressed = 1;
#endif
        QHash<int, QByteArray> svgBuffers;
        if (d->svgBuffers)
            svgBuffers = *d->svgBuffers;
        foreach(int key, d->svgFiles.keys()) {
            QByteArray buf;
            QFile f(d->svgFiles.value(key));
            if (f.open(QIODevice::ReadOnly))
                buf = f.readAll();
#ifndef QT_NO_COMPRESS
            buf = qCompress(buf);
#endif
            svgBuffers.insert(key, buf);
        }
        out << d->svgFiles << isCompressed << svgBuffers;
        if (d->addedPixmaps)
            out << (int)1 << *d->addedPixmaps;
        else
            out << (int)0;
    }
    else {
        QByteArray buf;
        if (d->svgBuffers)
            buf = d->svgBuffers->value(d->hashKey(QIcon::Normal, QIcon::Off));
        if (buf.isEmpty()) {
            QString svgFile = d->svgFiles.value(d->hashKey(QIcon::Normal, QIcon::Off));
            if (!svgFile.isEmpty()) {
                QFile f(svgFile);
                if (f.open(QIODevice::ReadOnly))
                    buf = f.readAll();
            }
        }
#ifndef QT_NO_COMPRESS
        buf = qCompress(buf);
#endif
        out << buf;
        // 4.3 has buggy handling of added pixmaps, so don't write any
        out << (int)0;
    }
    return true;
}

QT_END_NAMESPACE

#endif // QT_NO_SVGRENDERER
