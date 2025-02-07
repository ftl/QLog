#ifndef PROPCONDITIONS_H
#define PROPCONDITIONS_H

#include <QObject>
#include <QDateTime>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

template<class T>
class GenericValueMap
{
public:
    explicit GenericValueMap() {}
    ~GenericValueMap(){};

    struct MapPoint
    {
        double longitude;
        double latitude;
        T value;
    };

    virtual void addPoint(double longitude,
                          double latitude,
                          const T &value,
                          const T *skipValue = nullptr)
    {
        if ( skipValue && value == *skipValue )
            return;

        GenericValueMap::MapPoint point;
        point.longitude = longitude;
        point.latitude = latitude;
        point.value = value;

        map.append(point);
    };

    QList<GenericValueMap::MapPoint> getMap() const
    {
        return map;
    };

    void clear()
    {
        map.clear();
    };

    int count() const
    {
        return map.size();
    };

private:
    QList<GenericValueMap::MapPoint> map;
};

class PropConditions : public QObject
{
    Q_OBJECT
public:
    explicit PropConditions(QObject *parent = nullptr);
    ~PropConditions();
    bool isFluxValid();
    bool isKIndexValid();
    bool isAIndexValid();
    bool isAuroraMapValid();
    bool isMufMapValid();
    int getFlux();
    int getAIndex();
    double getKIndex();
    QList<GenericValueMap<double>::MapPoint> getAuroraPoints() const;
    QList<GenericValueMap<double>::MapPoint> getMUFPoints() const;

    static QString solarSummaryFile();

signals:
    void conditionsUpdated();
    void fluxUpdated();
    void KIndexUpdated();
    void AIndexUpdated();
    void auroraMapUpdated();
    void mufMapUpdated();

public slots:
    void update();
    void processReply(QNetworkReply* reply);

private:
    QDateTime flux_last_update;
    QDateTime k_index_last_update;
    QDateTime a_index_last_update;
    QDateTime auroraMap_last_update;
    QDateTime mufMap_last_update;
    int flux;
    int a_index;
    double k_index;
    GenericValueMap<double> auroraMap;
    GenericValueMap<double> mufMap;
    QHash<QUrl, int> failedRequests;

    void repeateRequest(const QUrl &);

private:
    QNetworkAccessManager* nam;
};

#endif // PROPCONDITIONS_H
