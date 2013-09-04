#ifndef SQLITEDB_H
#define SQLITEDB_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>

class SQLiteDB : public QObject
{
    Q_OBJECT
public:
    explicit SQLiteDB(QObject *parent = 0);

    bool open();
    bool initialize();
    QSqlError getLastError();

private:
    QSqlDatabase db;

signals:

public slots:

};

#endif // SQLITEDB_H
