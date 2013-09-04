#include "sqlitedb.h"

#include <QDebug>
#include <QDir>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QStandardPaths>
#else
#include <QDesktopServices>
#define QStandardPaths QDesktopServices
#define writableLocation storageLocation
#endif

#include <QSettings>
#include <QSqlQuery>

static const char create_adjacent[] = "CREATE TABLE IF NOT EXISTS adjacent_objects ("
                                      "parent_id INTEGER NOT NULL REFERENCES object_node(object_id) ON DELETE CASCADE,"
                                      "child_id INTEGER NOT NULL REFERENCES object_node(object_id) ON DELETE CASCADE)";

static const char create_obj_node[] = "CREATE TABLE IF NOT EXISTS object_node ("
                                      "object_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                      "type INTEGER NOT NULL,"
                                      "title TEXT,"
                                      "child_count INTEGER NOT NULL DEFAULT 0,"
                                      "reference_count INTEGER NOT NULL DEFAULT 0);";

static const char create_sources[] = "CREATE TABLE IF NOT EXISTS sources ("
                                     "object_id INTEGER PRIMARY KEY REFERENCES object_node(object_id) ON DELETE CASCADE,"
                                     "path TEXT UNIQUE NOT NULL CHECK (LENGTH(path) > 0),"
                                     "size INTEGER,"
                                     "date_created TIMESTAMP,"
                                     "date_modified TIMESTAMP)";

static const char create_music[] = "CREATE TABLE IF NOT EXISTS music ("
                                   "object_id INTEGER PRIMARY KEY REFERENCES object_node(object_id) ON DELETE CASCADE,"
                                   "file_format INTEGER,"
                                   "audio_bitrate INTEGER,"
                                   "audio_codec INTEGER,"
                                   "duration INTEGER,"
                                   "genre_id INTEGER REFERENCES object_node(object_id) ON DELETE SET NULL,"
                                   "track_id INTEGER REFERENCES object_node(object_id) ON DELETE SET NULL,"
                                   "artist_id INTEGER REFERENCES object_node(object_id) ON DELETE SET NULL,"
                                   "album_id INTEGER REFERENCES object_node(object_id) ON DELETE SET NULL,"
                                   "artist TEXT,"
                                   "album TEXT,"
                                   "track_number INTEGER)";

static const char create_photos[] = "CREATE TABLE IF NOT EXISTS photos ("
                                    "object_id INTEGER PRIMARY KEY REFERENCES object_node(object_id) ON DELETE CASCADE,"
                                    "date_created TIMESTAMP,"
                                    "file_format INTEGER,"
                                    "photo_codec INTEGER,"
                                    "width INTEGER,"
                                    "height INTEGER)";

static const char create_videos[] = "CREATE TABLE IF NOT EXISTS video ("
                                    "object_id INTEGER PRIMARY KEY REFERENCES object_node(object_id) ON DELETE CASCADE,"
                                    "file_format INTEGER,"
                                    "parental_level INTEGER,"
                                    "explanation TEXT,"
                                    "copyright TEXT,"
                                    "width INTEGER,"
                                    "height INTEGER,"
                                    "video_codec INTEGER,"
                                    "video_bitrate INTEGER,"
                                    "audio_codec INTEGER,"
                                    "audio_bitrate INTEGER,"
                                    "duration INTEGER)";

static const char create_savedata[] = "CREATE TABLE IF NOT EXISTS savedata ("
                                      "object_id INTEGER PRIMARY KEY REFERENCES object_node(object_id) ON DELETE CASCADE,"
                                      "detail TEXT,"
                                      "dir_name TEXT,"
                                      "title TEXT,"
                                      "date_updated TIMESTAMP)";

static const char create_trigger_node[] = "CREATE TRIGGER IF NOT EXISTS trg_objnode_deletechilds BEFORE DELETE ON object_node "
                                          "FOR EACH ROW BEGIN "
                                            "DELETE FROM object_node WHERE object_id IN "
                                              "(SELECT child_id FROM adjecent_objects WHERE parent_id == OLD.parent_id);"
                                          "END";

static const char create_trigger_adjins[] = "CREATE TRIGGER IF NOT EXISTS trg_adjacentobjects_ins AFTER INSERT ON adjacent_objects "
                                            "FOR EACH ROW BEGIN "
                                              "UPDATE object_node SET child_count = child_count + 1 WHERE object_id = NEW.parent_id;"
                                              "UPDATE object_node SET reference_count = reference_count + 1 WHERE object_id = NEW.child_id;"
                                            "END";

static const char create_trigger_adjdel[] = "CREATE TRIGGER IF NOT EXISTS trg_adjacentobjects_del AFTER DELETE ON adjacent_objects "
                                            "FOR EACH ROW BEGIN "
                                              "UPDATE object_node SET child_count = child_count - 1 WHERE object_id = OLD.parent_id;"
                                              "UPDATE object_node SET reference_count = reference_count - 1 WHERE object_id = OLD.child_id;"
                                              "DELETE FROM object_node WHERE object_id = OLD.parent_id AND child_count <= 0;"
                                              "DELETE FROM object_node WHERE object_id = OLD.child_id AND reference_count <= 0;"
                                            "END";

static const char *table_list[] = {
    create_adjacent, create_obj_node, create_sources,
    create_music, create_photos, create_videos, create_savedata
};

static const char *trigger_list[] = {
    create_trigger_node, create_trigger_adjins, create_trigger_adjdel
};

SQLiteDB::SQLiteDB(QObject *parent) :
    QObject(parent)
{
}

bool SQLiteDB::open()
{
    // fetch a configured database path if it exists
    QString db_path = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    db_path = QSettings().value("databasePath", db_path).toString();
    QDir(QDir::root()).mkpath(db_path);

    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(db_path + QDir::separator() + "qcma.sqlite");

    return db.open();
}

bool SQLiteDB::initialize()
{
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query;

    for(unsigned int i = 0; i < sizeof(table_list) / sizeof(const char *); i++) {
        if(!query.exec(table_list[i])) {
            return false;
        }
    }

    for(unsigned int i = 0; i < sizeof(trigger_list) / sizeof(const char *); i++) {
        if(!query.exec(trigger_list[i])) {
            return false;
        }
    }
    return true;
}

QSqlError SQLiteDB::getLastError()
{
    return db.lastError();
}
