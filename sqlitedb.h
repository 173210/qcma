#ifndef SQLITEDB_H
#define SQLITEDB_H

#include <vitamtp.h>

#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>

class SQLiteDB : public QObject
{
    Q_OBJECT
public:
    explicit SQLiteDB(QObject *parent = 0);

    bool open();
    int create();
    bool initialize();
    QSqlError getLastError();

    int getPathId(const QString &path);
    QString getPathFromId(int ohfi);
    bool updateSize(int ohfi, quint64 size);
    bool deleteEntry(int ohfi);
    bool deleteEntry(const QString &path);
    uint insertObjectEntry(const char *title, int type);
    bool insertSourceEntry(uint object_id, const QString &path);
    bool insertMusicEntry(const QString &path, int type);
    bool insertVideoEntry(const QString &path, int type);
    bool insertPhotoEntry(const QString &path, int type);
    bool insertSavedataEntry(const QString &path, int type);

private:
    QSqlDatabase db;

signals:

public slots:

};

#endif // SQLITEDB_H
