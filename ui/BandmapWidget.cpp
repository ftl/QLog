#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QMutableMapIterator>
#include <QMenu>
#include <QTextDocument>
#include <QScrollBar>
#include <QGraphicsSceneMouseEvent>
#include <algorithm>
#include <QWheelEvent>

#include "BandmapWidget.h"
#include "ui_BandmapWidget.h"
#include "core/Rig.h"
#include "data/Data.h"
#include "core/debug.h"

MODULE_IDENTIFICATION("qlog.ui.bandmapwidget");


//Maximal refresh rate for bandmap is 1s
#define BANDMAP_MAX_REFRESH_TIME 1000

//Maximal Aging interval is 20s
#define BANDMAP_AGING_CHECK_TIME 20000

//Pixel between each step in BandMap
#define PIXELSPERSTEP 10

BandmapWidget::BandmapWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::BandmapWidget),
    rxMark(nullptr),
    txMark(nullptr),
    keepRXCenter(true),
    pendingSpots(0),
    lastStationUpdate(0),
    bandmapAnimation(true)
{
    FCT_IDENTIFICATION;

    QSettings settings;

    ui->setupUi(this);

    double freq = settings.value("newcontact/frequency", 3.5).toDouble();
    freq += RigProfilesManager::instance()->getCurProfile1().ritOffset;

    keepRXCenter = settings.value("bandmap/centerrx", true).toBool();

    setBand(Data::band(freq), false);

    bandmapScene = new GraphicsScene(this);
    bandmapScene->setFocusOnTouch(false);

    connect(bandmapScene, &GraphicsScene::spotClicked,
            this, &BandmapWidget::spotClicked);
    connect(ui->scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, &BandmapWidget::focusZoomFreq);

    ui->graphicsView->setScene(bandmapScene);
    ui->graphicsView->installEventFilter(this);
    //ui->scrollArea->verticalScrollBar()->setSingleStep(5);

    ui->clearSpotOlderSpin->setValue(settings.value("bandmap/spot_aging", 0).toInt());

    Rig* rig = Rig::instance();
    connect(rig, &Rig::frequencyChanged,
            this, &BandmapWidget::updateTunedFrequency);

    update_timer = new QTimer;
    connect(update_timer, &QTimer::timeout, this, &BandmapWidget::updateStationTimer);
    update_timer->start(BANDMAP_MAX_REFRESH_TIME);

    updateTunedFrequency(VFO1, freq, freq, freq);
    update();
}

void BandmapWidget::update()
{
    FCT_IDENTIFICATION;

    /****************
     * Restart Time *
     ****************/
    update_timer->setInterval(BANDMAP_MAX_REFRESH_TIME);

    /*************
     * Clear All *
     *************/
    clearAllCallsignFromScene();

    clearFreqMark(&rxMark);
    clearFreqMark(&txMark);

    bandmapScene->clear();

    // do not show bandmap for submm bands
    if ( rx_freq > 250000.0 || currentBand.start >= 300000.0 )
    {
        return;
    }

    /*******************
     * Determine Scale *
     *******************/
    double step;
    int digits;

    determineStepDigits(step, digits);

    int steps = static_cast<int>(round((currentBand.end - currentBand.start) / step));

    ui->graphicsView->setFixedSize(330, steps * PIXELSPERSTEP + 30);

    /****************/
    /* Draw bandmap */
    /****************/
    for ( int i = 0; i <= steps; i++ )
    {
        bandmapScene->addLine(0,
                              i * PIXELSPERSTEP,
                              (i % 5 == 0) ? 15 : 10,
                              i * PIXELSPERSTEP,
                              QPen(QColor(192,192,192)));

        if (i % 5 == 0)
        {
            QGraphicsTextItem* text = bandmapScene->addText(QString::number(currentBand.start + step*i, 'f', digits));
            text->setPos(- (text->boundingRect().width()) - 10,
                         i * PIXELSPERSTEP - (text->boundingRect().height() / 2));
        }
    }

    QString endFreqDigits= QString::number(currentBand.end + step*steps, 'f', digits);
    bandmapScene->setSceneRect(160 - (endFreqDigits.size() * PIXELSPERSTEP),
                               0,
                               0,
                               steps * PIXELSPERSTEP + 20);

    /************************/
    /* Draw TX and RX Marks */
    /************************/
    drawTXRXMarks(step);

    /*****************
     * Draw Stations *
     *****************/
    updateStations();
}

void BandmapWidget::spotAging()
{
    FCT_IDENTIFICATION;

    int clear_interval_sec = ui->clearSpotOlderSpin->value() * 60;

    qCDebug(function_parameters)<<clear_interval_sec;

    if ( clear_interval_sec <= 0 ) return;

    QMutableMapIterator<double, DxSpot> spotIterator(spots);

    while ( spotIterator.hasNext() )
    {
        spotIterator.next();
        //clear spots automatically
        if ( spotIterator.value().time.addSecs(clear_interval_sec) <= QDateTime::currentDateTimeUtc() )
        {
            spotIterator.remove();
        }
    }
}

void BandmapWidget::updateStations()
{
    FCT_IDENTIFICATION;

    double step;
    int digits;
    double min_y = 0;

    /****************
     * Restart Time *
     ****************/
    update_timer->setInterval(BANDMAP_MAX_REFRESH_TIME);

    clearAllCallsignFromScene();

    spotAging();

    // do not show bandmap for submm bands
    if ( rx_freq > 250000.0 || currentBand.start >= 300000.0 )
    {
        return;
    }

    determineStepDigits(step, digits);

    QMap<double, DxSpot>::iterator lower = spots.lowerBound(currentBand.start);
    QMap<double, DxSpot>::iterator upper = spots.upperBound(currentBand.end);

    for (; lower != upper; lower++)
    {
        double freq_y = ((lower.key() - currentBand.start) / step) * PIXELSPERSTEP;
        double text_y = std::max(min_y + 5, freq_y);

        /*************************
         * Draw Line to Callsign *
         *************************/
        lineItemList.append(bandmapScene->addLine(17,
                                                  freq_y,
                                                  40,
                                                  text_y,
                                                  QPen(QColor(192,192,192))));

        QString callsignTmp = lower.value().callsign;
        QString timeTmp = lower.value().time.toString(locale.formatTimeShort());

        QGraphicsTextItem* text = bandmapScene->addText(callsignTmp + " @ " + timeTmp);
        text->document()->setDocumentMargin(0);
        text->setPos(40, text_y - (text->boundingRect().height() / 2));
        text->setFlags(QGraphicsItem::ItemIsFocusable |
                       QGraphicsItem::ItemIsSelectable |
                       text->flags());
        text->setProperty("freq", lower.key());
        text->setToolTip(lower.value().comment);

        min_y = text_y + text->boundingRect().height() / 2;

        QColor textColor = Data::statusToColor(lower.value().status, qApp->palette().color(QPalette::Text));
        text->setDefaultTextColor(textColor);
        textItemList.append(text);
    }

    pendingSpots = 0;
    lastStationUpdate = QDateTime::currentMSecsSinceEpoch();
}

void BandmapWidget::determineStepDigits(double &step, int &digits) const
{
    FCT_IDENTIFICATION;

    switch (zoom) {
    case ZOOM_100HZ: step = 0.0001; digits = 4; break;
    case ZOOM_250HZ: step = 0.00025; digits = 4; break;
    case ZOOM_500HZ: step = 0.0005; digits = 4; break;
    case ZOOM_1KHZ: step = 0.001; digits = 3; break;
    case ZOOM_2K5HZ: step = 0.0025; digits = 3; break;
    case ZOOM_5KHZ: step = 0.005; digits = 3; break;
    case ZOOM_10KHZ: step = 0.01; digits = 2; break;
    }

    /* bands below are too wide for BandMap, therefore it is needed to short them */
    if ( currentBand.start >= 28.0 && currentBand.start < 420.0 )
    {
        step = step * 10;
    }
    if ( ( currentBand.start >= 420.0 && currentBand.start < 2300.0 )
         || currentBand.start == 119980 )
    {
        step = step * 100;
    }
    else if ( currentBand.start >= 2300.0 && currentBand.start < 75500.0 )
    {
        step = step * 1000;
    }
    else if (currentBand.start == 75500.0 || currentBand.start >= 142000.0)
    {
        step = step * 10000;
    }
}

void BandmapWidget::clearAllCallsignFromScene()
{
    FCT_IDENTIFICATION;

    QMutableListIterator<QGraphicsLineItem*> lineIterator(lineItemList);

    while ( lineIterator.hasNext() )
    {
        lineIterator.next();
        bandmapScene->removeItem(lineIterator.value());
        delete lineIterator.value();
    }

    lineItemList.clear();

    QMutableListIterator<QGraphicsTextItem*> textIterator(textItemList);

    while ( textIterator.hasNext() )
    {
        textIterator.next();
        bandmapScene->removeItem(textIterator.value());
        delete textIterator.value();
    }

    textItemList.clear();
}

void BandmapWidget::clearFreqMark(QGraphicsPolygonItem **currentPolygon)
{
    FCT_IDENTIFICATION;

    if ( *currentPolygon != nullptr )
    {
        bandmapScene->removeItem(*currentPolygon);
        delete *currentPolygon;
        *currentPolygon = nullptr;
    }
}

void BandmapWidget::drawFreqMark(const double freq,
                                 const double step,
                                 const QColor &color,
                                 QGraphicsPolygonItem **currentPolygon)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << freq << step << color;

    clearFreqMark(currentPolygon);

    /* do not show the freq mark if it is outside the bandmap */
    if ( freq < currentBand.start || freq > currentBand.end )
    {
        return;
    }

    int Yposition = Freq2ScenePos(freq).y();

    QPolygonF poly;
    poly << QPointF(-1, Yposition)
         << QPointF(-7, Yposition - 7)
         << QPointF(-7, Yposition + 7);

    *currentPolygon = bandmapScene->addPolygon(poly,
                                              QPen(Qt::NoPen),
                                              QBrush(color, Qt::SolidPattern));
}

void BandmapWidget::drawTXRXMarks(double step)
{
    FCT_IDENTIFICATION;

    // do not show bandmap for submm bands
    if ( rx_freq > 250000.0 || currentBand.start >= 300000.0 )
    {
        return;
    }

    /**************************/
    /* Draw RX frequency mark */
    /**************************/
    drawFreqMark(rx_freq, step, QColor(30, 180, 30), &rxMark);

    centerRXFreqPosition();

    /**************************/
    /* Draw TX frequency mark */
    /**************************/
    if ( tx_freq >= currentBand.start
         && tx_freq <= currentBand.end
         && tx_freq != rx_freq )
    {
        drawFreqMark(tx_freq, step, QColor(255, 0, 0), &txMark);
    }
    else
    {
        clearFreqMark(&txMark);
    }
}

void BandmapWidget::removeDuplicates(DxSpot &spot)
{
    FCT_IDENTIFICATION;

    QMap<double, DxSpot>::iterator lower = spots.lowerBound(spot.freq - 0.005);
    QMap<double, DxSpot>::iterator upper = spots.upperBound(spot.freq + 0.005);

    while ( lower != upper )
    {
        if ( lower.value().callsign.compare(spot.callsign, Qt::CaseInsensitive) == 0 )
        {
            lower = spots.erase(lower);
        }
        else
        {
            ++lower;
        }
    }
}

void BandmapWidget::addSpot(DxSpot spot)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << spot.freq << spot.callsign;

    this->removeDuplicates(spot);
    spots.insert(spot.freq, spot);

    if ( spot.band == currentBand.name )
    {
        qint64 currTime = QDateTime::currentMSecsSinceEpoch();

        /* if the spots are received slowly, this will guarantee
         * that Spots will be displayed as soon as they are received.
         * QLog does not have to wait for the timer to tick to update the stations.
         */
        if ( currTime -  BANDMAP_MAX_REFRESH_TIME >= lastStationUpdate )
        {
            updateStations();
        }
        else
        {
            /* If the spot are received quickly then store them and wait for QTimer tick */
            pendingSpots++;
        }
    }
}

void BandmapWidget::updateStationTimer()
{
    FCT_IDENTIFICATION;

    /* This function handle QTime tick to update Stations */

    qint64 currTime = QDateTime::currentMSecsSinceEpoch();

    /* If there is (are) station(s) or Time to Aging occured then update the bandmap */
    if ( pendingSpots > 0
         || currTime - BANDMAP_AGING_CHECK_TIME >= lastStationUpdate )
    {
        updateStations();
    }
}

DxSpot BandmapWidget::nearestSpot(const double freq) const
{
    FCT_IDENTIFICATION;

    QMap<double, DxSpot>::const_iterator it = spots.constFind(freq);

    if( it == spots.cend() )
    {
        QMap<double, DxSpot>::const_iterator lower = spots.lowerBound(freq - Hz2MHz(kHz(1)));
        QMap<double, DxSpot>::const_iterator upper = spots.upperBound(freq + Hz2MHz(kHz(1)));

        it = std::min_element( lower, upper,
                [freq](const DxSpot &p1,
                       const DxSpot &p2)
                {
                return
                    qAbs(p1.freq - freq) <
                    qAbs(p2.freq - freq);
                });

        if ( it != upper )
        {
            /* FOUND */
            return it.value();
        }
        else
        {
            /* Not found */
            return DxSpot();
        }
    }

    /* Exact Match */
    return it.value();
}

void BandmapWidget::updateNearestSpot()
{
    FCT_IDENTIFICATION;

    static DxSpot lastNearestSpot;
    DxSpot currNearestSpot;

    currNearestSpot = nearestSpot(rx_freq);

    if ( currNearestSpot.callsign != lastNearestSpot.callsign )
    {
        emit nearestSpotFound(currNearestSpot);
        lastNearestSpot = currNearestSpot;
    }
}

void BandmapWidget::setBandmapAnimation(bool isEnable)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << isEnable;

    bandmapAnimation = isEnable;
}

void BandmapWidget::setBand(const Band &newBand, bool savePrevBandZoom)
{
    FCT_IDENTIFICATION;

    if ( savePrevBandZoom )
    {
        saveCurrentZoom();
    }
    currentBand = newBand;
    zoom = savedZoom(newBand);
}

void BandmapWidget::saveCurrentZoom()
{
    FCT_IDENTIFICATION;

    QSettings settings;

    settings.setValue("bandmap/zoom/" + currentBand.name, zoom);
}

BandmapWidget::BandmapZoom BandmapWidget::savedZoom(Band band)
{
    FCT_IDENTIFICATION;

    QSettings settings;

    QVariant zoomVariant = settings.value("bandmap/zoom/" + band.name, ZOOM_10KHZ);
    return zoomVariant.value<BandmapWidget::BandmapZoom>();
}

void BandmapWidget::spotAgingChanged(int)
{
    FCT_IDENTIFICATION;

    QSettings settings;

    settings.setValue("bandmap/spot_aging", ui->clearSpotOlderSpin->value());
}

void BandmapWidget::clearSpots()
{
    FCT_IDENTIFICATION;

    spots.clear();
    updateStations();
    updateNearestSpot();
}

void BandmapWidget::zoomIn()
{
    FCT_IDENTIFICATION;

    if ( zoomFreq == 0.0 )
    {
        if ( keepRXCenter )
        {
            zoomFreq = rx_freq;
        }
        else
        {
            QPoint point(0,ui->scrollArea->verticalScrollBar()->value() + this->height()/2 - 50);
            zoomFreq = ScenePos2Freq(ui->graphicsView->mapToScene(point));
        }
        zoomWidgetYOffset = this->height()/2 - 50;
    }

    if ( zoom > ZOOM_100HZ )
    {
        zoom = static_cast<BandmapZoom>(static_cast<int>(zoom) - 1);
    }
    setBandmapAnimation(false);
    update();
    setBandmapAnimation(true);
}

void BandmapWidget::zoomOut()
{
    FCT_IDENTIFICATION;

    if ( zoomFreq == 0.0 )
    {
        if ( keepRXCenter )
        {
            zoomFreq = rx_freq;
        }
        else
        {
            QPoint point(0,ui->scrollArea->verticalScrollBar()->value() + this->height()/2 - 50);
            zoomFreq = ScenePos2Freq(ui->graphicsView->mapToScene(point));
        }
        zoomWidgetYOffset = this->height()/2 - 50;
    }

    if ( zoom < ZOOM_10KHZ )
    {
        zoom = static_cast<BandmapZoom>(static_cast<int>(zoom) + 1);
    }
    setBandmapAnimation(false);
    update();
    setBandmapAnimation(true);
}

void BandmapWidget::spotsDxccStatusRecal(const QSqlRecord &record)
{
    FCT_IDENTIFICATION;

    qint32 dxcc = record.value("dxcc").toInt();
    QString band = record.value("band").toString();
    QString mode = Data::instance()->modeToDXCCMode(record.value("mode").toString());

    QMutableMapIterator<double, DxSpot> spotIterator(spots);

    while ( spotIterator.hasNext() )
    {
        spotIterator.next();
        spotIterator.value().status = Data::dxccFutureStatus(spotIterator.value().status,
                                                             spotIterator.value().dxcc.dxcc,
                                                             spotIterator.value().band,
                                                             Data::freqToDXCCMode(spotIterator.value().freq),
                                                             dxcc,
                                                             band,
                                                             mode);
    }
    updateStations();
}

void BandmapWidget::focusZoomFreq(int, int)
{
    FCT_IDENTIFICATION;

    if ( zoomFreq > 0.0 )
    {
        int newScrollValue = qMin(qMax(Freq2ScenePos(zoomFreq).y() - ( zoomWidgetYOffset ), 0.0),
                                  (double)ui->scrollArea->verticalScrollBar()->maximum());
        ui->scrollArea->verticalScrollBar()->setValue(newScrollValue);
        zoomFreq = 0.0;
    }
}

void BandmapWidget::spotClicked(const QString &call,
                                double freq)
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << call << freq;
    qCDebug(runtime) << "Last Tuned DX" << lastTunedDX.callsign << lastTunedDX.freq;

    /* Do not emit the Spot two times - double click*/
    if ( lastTunedDX.callsign == call
         && lastTunedDX.freq == freq )
        return;

    emit tuneDx(call, freq);
    lastTunedDX.callsign = call;
    lastTunedDX.freq = freq;
}

void BandmapWidget::showContextMenu(const QPoint &point)
{
    FCT_IDENTIFICATION;

    if ( ui->graphicsView->itemAt(point) )
    {
        return;
    }

    QMenu contextMenu(this);
    QMenu bandsMenu(tr("Show Band"), &contextMenu);

    for (Band &enabledBand : Data::bandsList(false, true))
    {
        QAction* action = new QAction(enabledBand.name);
        connect(action, &QAction::triggered, this, [this, enabledBand]()
        {
            setBand(enabledBand);
            this->update();
        });
        bandsMenu.addAction(action);
    }

    QAction* centerRXAction = new QAction(tr("Center RX"), &contextMenu);
    centerRXAction->setCheckable(true);
    centerRXAction->setChecked(keepRXCenter);
    connect(centerRXAction, &QAction::triggered, this, &BandmapWidget::centerRXActionChecked);

    contextMenu.addMenu(&bandsMenu);
    contextMenu.addAction(centerRXAction);

    contextMenu.exec(ui->graphicsView->mapToGlobal(point));
}

void BandmapWidget::updateTunedFrequency(VFOID vfoid, double vfoFreq, double ritFreq, double xitFreq)
{
    FCT_IDENTIFICATION;

    Q_UNUSED(vfoid)

    qCDebug(function_parameters) << vfoFreq << ritFreq << xitFreq;

    /* always show the bandmap for RIT Freq */
    rx_freq = ritFreq;
    tx_freq = xitFreq;

    if ( rx_freq < currentBand.start || rx_freq > currentBand.end )
    {
        /* Operator switched a band */
        Band newBand = Data::band(rx_freq);
        if ( !newBand.name.isEmpty() )
        {
            setBand(newBand);
        }
        /**********************/
        /* Redraw all bandmap */
        /**********************/
        update();
    }
    else
    {
        /* Operator does not change a band */
        double step;
        int digits;

        determineStepDigits(step, digits);

        /************************/
        /* Draw TX and RX Marks */
        /************************/
        drawTXRXMarks(step);
    }

    updateNearestSpot();
}

BandmapWidget::~BandmapWidget()
{
    FCT_IDENTIFICATION;

    if ( update_timer )
    {
        update_timer->stop();
        update_timer->deleteLater();
    }

    saveCurrentZoom();

    delete ui;
}

void BandmapWidget::resizeEvent(QResizeEvent *event)
{
    FCT_IDENTIFICATION;

    QWidget::resizeEvent(event);

    centerRXFreqPosition();
}

bool BandmapWidget::eventFilter(QObject *, QEvent *event)
{
    FCT_IDENTIFICATION;

    if (event->type() == QEvent::Wheel)
    {
        QWheelEvent *wheelEvent = dynamic_cast<QWheelEvent*>(event);

        if ( wheelEvent )
        {
            if ( QApplication::keyboardModifiers() == Qt::ControlModifier )
            {
                /*
                 * CRTL + Mouse Wheel
                 *
                 * Zoom In/Out
                 */

                QPoint zoomViewPoint = ui->graphicsView->mapFromGlobal(QCursor::pos());
                zoomWidgetYOffset = zoomViewPoint.y() - ui->scrollArea->verticalScrollBar()->value();
                zoomFreq = ScenePos2Freq(ui->graphicsView->mapToScene(zoomViewPoint));

                QPoint wheelDelta(wheelEvent->angleDelta());

                if ( wheelDelta.y() > 0 )
                {
                    zoomIn();
                }

                if ( wheelDelta.y() < 0 )
                {
                    zoomOut();
                }

                /*
                 * DO NOT focus zoomed Freq here because the scrollbar
                 * is not resized yet and it is not possible to compute
                 * a correct value for scrollbar value (scrollbar min/max
                 * is recomputed later and it emits RangeChanged signal).
                 * SO focus zoomed Freq in SLOT for RangeChanged signal.
                 */
                event->accept();
                return true;
            }
        }
    }
    return false;
}

void BandmapWidget::centerRXFreqPosition()
{
    FCT_IDENTIFICATION;

    qreal freqScenePos = Freq2ScenePos(rx_freq).y();

    QPropertyAnimation *anim = new QPropertyAnimation(ui->scrollArea->verticalScrollBar(), "value", this);
    anim->setDuration((bandmapAnimation) ? 300 : 0);
    anim->setStartValue(ui->scrollArea->verticalScrollBar()->value());

    if ( keepRXCenter )
    {

        /* If RX freq should be center then center it */
        anim->setEndValue(freqScenePos - (this->height()/2) + 50);
        //ui->scrollArea->verticalScrollBar()->setValue(freqScenePos - (this->height()/2) + 50);
    }
    else
    {
        /* If RX freq is out-of-scene then keep the RX mark visible - this is not centering !!! */
        int sceneSize = this->height() - 60;
        int sliderSceneMin = ui->scrollArea->verticalScrollBar()->value();
        int sliderSceneMax = ui->scrollArea->verticalScrollBar()->value() + sceneSize;

        if ( freqScenePos < sliderSceneMin )
        {
            anim->setEndValue(sliderSceneMin - (sliderSceneMin - freqScenePos) - 40);
        }
        else if ( freqScenePos > sliderSceneMax - 20 ) //asymetric becuase possible slider below
        {
            anim->setEndValue(sliderSceneMin + (freqScenePos - sliderSceneMax) + 60);
        }
        else
        {
            anim->setEndValue(ui->scrollArea->verticalScrollBar()->value());
        }
    }
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

QPointF BandmapWidget::Freq2ScenePos(const double freq) const
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << freq;

    if ( freq < currentBand.start || freq > currentBand.end )
    {
        return QPointF();
    }

    double step;
    int digits;

    determineStepDigits(step, digits);

    QPointF ret(0, ((freq - currentBand.start) / step) * PIXELSPERSTEP);

    qCDebug(runtime) << ret;

    return ret;
}

double BandmapWidget::ScenePos2Freq(const QPointF &point) const
{
    FCT_IDENTIFICATION;

    qCDebug(function_parameters) << point;

    double step;
    int digits;

    determineStepDigits(step, digits);

    double ret = currentBand.start + (point.y() / PIXELSPERSTEP) * step;

    if ( ret > currentBand.end )
    {
        ret = currentBand.end;
    }

    if ( ret < currentBand.start )
    {
        ret = currentBand.start;
    }

    qCDebug(runtime) << ret;

    return ret;
}

void BandmapWidget::centerRXActionChecked(bool state)
{
    FCT_IDENTIFICATION;

    QSettings settings;

    keepRXCenter = state;
    settings.setValue("bandmap/centerrx", keepRXCenter);

    zoomFreq = 0.0;
    centerRXFreqPosition();
}

void GraphicsScene::mousePressEvent(QGraphicsSceneMouseEvent *evt)
{
    FCT_IDENTIFICATION;

    if ( (evt->buttons() & (Qt::MiddleButton | Qt::RightButton) ) )
    {
        evt->accept();
    }
    else
    {
        QGraphicsItem *item = itemAt(evt->scenePos(), QTransform());
        QGraphicsTextItem *focusedSpot = dynamic_cast<QGraphicsTextItem*>(item);

        if ( focusedSpot )
        {
            emit spotClicked(focusedSpot->toPlainText().split(" ").first(),
                             focusedSpot->property("freq").toDouble());

        }
        evt->accept();
        //QGraphicsScene::mousePressEvent(evt);
    }
}

void GraphicsScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *evt)
{
    FCT_IDENTIFICATION;

    evt->accept();
}
