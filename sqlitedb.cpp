#include "sqlitedb.h"
#include "sforeader.h"
#include "avdecoder.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

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

static const char create_videos[] = "CREATE TABLE IF NOT EXISTS videos ("
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

typedef struct {
    const char *file_ext;
    int file_format;
    int file_codec;
} file_type;

#define FILE_FORMAT_MP4 1
#define FILE_FORMAT_WAV 2
#define FILE_FORMAT_MP3 3
#define FILE_FORMAT_JPG 4
#define FILE_FORMAT_PNG 5
#define FILE_FORMAT_GIF 6
#define FILE_FORMAT_BMP 7
#define FILE_FORMAT_TIF 8

#define CODEC_TYPE_MPEG4 2
#define CODEC_TYPE_AVC 3
#define CODEC_TYPE_MP3 12
#define CODEC_TYPE_AAC 13
#define CODEC_TYPE_PCM 15
#define CODEC_TYPE_JPG 17
#define CODEC_TYPE_PNG 18
#define CODEC_TYPE_TIF 19
#define CODEC_TYPE_BMP 20
#define CODEC_TYPE_GIF 21

static const file_type audio_list[] = {
    {"mp3", FILE_FORMAT_MP3, CODEC_TYPE_MP3},
    {"mp4", FILE_FORMAT_MP4, CODEC_TYPE_AAC},
    {"wav", FILE_FORMAT_WAV, CODEC_TYPE_PCM}
};

static const file_type photo_list[] = {
    {"jpg",  FILE_FORMAT_JPG, CODEC_TYPE_JPG},
    {"jpeg", FILE_FORMAT_JPG, CODEC_TYPE_JPG},
    {"png",  FILE_FORMAT_PNG, CODEC_TYPE_PNG},
    {"tif",  FILE_FORMAT_TIF, CODEC_TYPE_TIF},
    {"tiff", FILE_FORMAT_TIF, CODEC_TYPE_TIF},
    {"bmp",  FILE_FORMAT_BMP, CODEC_TYPE_BMP},
    {"gif",  FILE_FORMAT_GIF, CODEC_TYPE_GIF},
};

static const char *video_list[] = {"mp4"};


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

    // force object_id to start at 256
    if(query.exec("INSERT INTO object_node (object_id, type) VALUES (255, 0)")) {
        query.exec("DELETE FROM object_node WHERE object_id = 255");
    }
    return true;
}

QSqlError SQLiteDB::getLastError()
{
    return db.lastError();
}

int SQLiteDB::create()
{
    int total_objects = 0;

    const int ohfi_array[] = { VITA_OHFI_MUSIC, VITA_OHFI_PHOTO, VITA_OHFI_VIDEO,
                               VITA_OHFI_BACKUP, VITA_OHFI_VITAAPP, VITA_OHFI_PSPAPP,
                               VITA_OHFI_PSPSAVE, VITA_OHFI_PSXAPP, VITA_OHFI_PSMAPP
                             };

    for(int i = 0, max = sizeof(ohfi_array) / sizeof(int); i < max; i++) {

        switch(ohfi_array[i]) {
        case VITA_OHFI_MUSIC:
            break;

        case VITA_OHFI_PHOTO:
            break;

        case VITA_OHFI_VIDEO:
            break;

        case VITA_OHFI_BACKUP:
        case VITA_OHFI_VITAAPP:
        case VITA_OHFI_PSPAPP:
        case VITA_OHFI_PSPSAVE:
        case VITA_OHFI_PSXAPP:
        case VITA_OHFI_PSMAPP:
            break;
        }
    }
    return total_objects;
}

int SQLiteDB::getPathId(const QString &path)
{
    if (!db.isOpen()) {
        return -1;
    }
    QSqlQuery query(QString("SELECT object_id from sources WHERE path = %1").arg(path));
    if(query.next()) {
        return query.value(0).toInt();
    } else {
        return -1;
    }
}

QString SQLiteDB::getPathFromId(int ohfi)
{
    if (!db.isOpen()) {
        return QString();
    }
    QSqlQuery query(QString("SELECT path FROM sources WHERE object_id = %1").arg(ohfi));
    if(query.next()) {
        return query.value(0).toString();
    } else {
        return QString();
    }
}

bool SQLiteDB::updateSize(int ohfi, quint64 size)
{
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(QString("UPDATE sources SET size = %1 WHERE object_id == %2").arg(size).arg(ohfi));
    return query.exec();
}

bool SQLiteDB::deleteEntry(int ohfi)
{
    QSqlQuery query(QString("DELETE FROM object_node WHERE object_id == %1").arg(ohfi));
    return query.exec();
}

bool SQLiteDB::deleteEntry(const QString &path)
{
    QSqlQuery query(QString("DELETE FROM object_node WHERE object_id == (SELECT object_id FROM sources WHERE path == %1)").arg(path));
    return query.exec();
}

uint SQLiteDB::insertObjectEntry(const char *title, int type)
{
    QSqlQuery query;
    query.prepare("REPLACE INTO object_node (type, title) VALUES (:type, :title)");
    query.bindValue(0, type);
    query.bindValue(1, title);

    if(query.exec() && query.exec("SELECT last_insert_rowid()") && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

bool SQLiteDB::insertSourceEntry(uint object_id, const QString &path)
{
    qint64 size;
    uint date_created, date_modified;

    QFileInfo info(path);
    size = info.size();
    date_created = info.created().toTime_t();
    date_modified = info.lastModified().toTime_t();

    QSqlQuery query;
    query.prepare("REPLACE INTO sources (object_id, path, size, date_created, date_modified)"
                  "VALUES (:object_id, :path, :size, :date_created, :date_modified)");
    query.bindValue(0, object_id);
    query.bindValue(1, path);
    query.bindValue(2, size);
    query.bindValue(3, date_created);
    query.bindValue(3, date_modified);
    return query.exec();
}

bool SQLiteDB::insertMusicEntry(const QString &path, int type)
{
    bool ok;
    uint ohfi;
    AVDecoder decoder;
    quint64 duration;
    const char *artist, *album, *albumartist, *genre, *track, *title;
    int file_format, audio_codec, audio_bitrate, genre_id, artist_id, track_id, album_id, track_number;

    if(!decoder.open(path)) {
        return false;
    }

    album = decoder.getMetadataEntry("album");
    genre = decoder.getMetadataEntry("genre");
    artist = decoder.getMetadataEntry("artist");
    albumartist = decoder.getMetadataEntry("albumartist");
    track = decoder.getMetadataEntry("track");

    track_number = QString(track).toInt(&ok);
    if(!ok) {
        // set track number to 1 by default
        track_number = 1;
    }
    audio_bitrate = decoder.getBitrate();
    duration = decoder.getDuration();
    file_format = 3;
    audio_codec = 17;

    QString basename = QFileInfo(path).baseName();
    title = decoder.getMetadataEntry("title", basename.toUtf8().constData());

    db.transaction();

    if((ohfi = insertObjectEntry(title, type)) == 0) {
        db.rollback();
        return false;
    }


    album_id = album ? insertObjectEntry(album, VITA_DIR_TYPE_MASK_MUSIC | VITA_DIR_TYPE_MASK_ROOT | VITA_DIR_TYPE_MASK_ALBUMS) : 0;
    genre_id = genre ? insertObjectEntry(genre, VITA_DIR_TYPE_MASK_MUSIC | VITA_DIR_TYPE_MASK_ROOT | VITA_DIR_TYPE_MASK_GENRES) : 0;
    track_id = artist ? insertObjectEntry(artist, VITA_DIR_TYPE_MASK_MUSIC | VITA_DIR_TYPE_MASK_ROOT | VITA_DIR_TYPE_MASK_ARTISTS) : 0;
    artist_id = 0;// albumartist ? insertObjectEntry(albumartist, VITA_DIR_TYPE_MASK_MUSIC | VITA_DIR_TYPE_MASK_ROOT | 0) : 0;

    if(!insertSourceEntry(ohfi, path)) {
        db.rollback();
        return false;
    }

    QSqlQuery query;
    query.prepare("REPLACE INTO music"
    "(object_id, file_format, audio_codec, audio_bitrate, duration, genre_id, artist_id, album_id, track_id, artist, album, track_number)"
    "VALUES (:object_id, :file_format, :audio_codec, :audio_bitrate, :duration, :genre_id, :artist_id, :album_id, :track_id, :artist, :album, :track_number)");

    query.bindValue(0, ohfi);
    query.bindValue(1, file_format);
    query.bindValue(2, audio_codec);
    query.bindValue(3, audio_bitrate);
    query.bindValue(4, duration);
    query.bindValue(5, genre_id ? genre_id : QVariant(QVariant::Int));
    query.bindValue(6, artist_id ? artist_id : QVariant(QVariant::Int));
    query.bindValue(7, album_id ? album_id : QVariant(QVariant::Int));
    query.bindValue(8, track_id ? track_id : QVariant(QVariant::Int));
    query.bindValue(9, artist);
    query.bindValue(10, album);
    query.bindValue(11, track_number);

    if(query.exec()) {
        db.commit();
        return true;
    } else {
        db.rollback();
        return false;
    }
}

bool SQLiteDB::insertVideoEntry(const QString &path, int type)
{
    int ohfi;
    AVDecoder decoder;
    quint64 duration;
    int file_format, parental_level, width, height, video_codec, video_bitrate, audio_codec, audio_bitrate;
    const char *explanation, *copyright, *title;

    if(!decoder.open(path) || !decoder.loadCodec(AVDecoder::CODEC_VIDEO)) {
        return false;
    }

    file_format = 1;
    parental_level = 0;
    explanation = decoder.getMetadataEntry("comments", "");
    copyright = decoder.getMetadataEntry("copyright", "");
    width = decoder.getWidth();
    height = decoder.getHeight();
    duration = decoder.getDuration();
    video_codec = 3;
    video_bitrate = decoder.getCodecBitrate();

    if(decoder.loadCodec(AVDecoder::CODEC_AUDIO)) {
        audio_codec = 13;
        audio_bitrate = decoder.getCodecBitrate();
    } else {
        audio_codec = 0;
        audio_bitrate = 0;
    }

    QString basename = QFileInfo(path).baseName();
    title = decoder.getMetadataEntry("title", basename.toUtf8().constData());

    db.transaction();

    if((ohfi = insertObjectEntry(title, type)) == 0) {
        db.rollback();
        return false;
    }

    if(!insertSourceEntry(ohfi, path)) {
        db.rollback();
        return false;
    }

    QSqlQuery query;
    query.prepare("REPLACE INTO videos"
    "(object_id, file_format, parental_level, explanation, copyright, width, height, video_codec, video_bitrate, audio_codec, audio_bitrate, duration)"
    "VALUES (:object_id, :file_format, :parental_level, :explanation, :copyright, :width, :height, :video_codec, :video_bitrate, :audio_codec, :audio_bitrate, :duration)");

    query.bindValue(0, ohfi);
    query.bindValue(1, parental_level);
    query.bindValue(2, explanation);
    query.bindValue(3, copyright);
    query.bindValue(4, width);
    query.bindValue(5, height);
    query.bindValue(5, video_codec);
    query.bindValue(6, video_bitrate);
    query.bindValue(7, audio_codec);
    query.bindValue(8, audio_bitrate);
    query.bindValue(9, duration);

    if(query.exec()) {
        db.commit();
        return true;
    } else {
        db.rollback();
        return false;
    }
}

bool SQLiteDB::insertPhotoEntry(const QString &path, int type)
{
    int ohfi;
    AVDecoder decoder;
    uint date_created;
    int width, height, file_format, photo_codec;
    const char *title;

    if(!decoder.open(path) || !decoder.loadCodec(AVDecoder::CODEC_VIDEO)) {
        return false;
    }

    date_created = QFileInfo(path).created().toTime_t();
    width = decoder.getWidth();
    height = decoder.getHeight();
    file_format = 4;
    photo_codec = 17;

    QString basename = QFileInfo(path).baseName();
    title = decoder.getMetadataEntry("title", basename.toUtf8().constData());

    db.transaction();

    if((ohfi = insertObjectEntry(title, type)) == 0) {
        db.rollback();
        return false;
    }

    if(!insertSourceEntry(ohfi, path)) {
        db.rollback();
        return false;
    }

    QSqlQuery query;
    query.prepare("REPLACE INTO photos"
    "(object_id, date_created, file_format, photo_codec, width, height)"
    "VALUES (:object_id, :date_created, :file_format, :photo_codec, :width, :height)");

    query.bindValue(0, ohfi);
    query.bindValue(1, date_created);
    query.bindValue(2, file_format);
    query.bindValue(3, photo_codec);
    query.bindValue(4, width);
    query.bindValue(5, height);

    if(query.exec()) {
        db.commit();
        return true;
    } else {
        db.rollback();
        return false;
    }
}

bool SQLiteDB::insertSavedataEntry(const QString &path, int type)
{
    int ohfi;

    uint date_updated;
    const char *title, *savedata_detail, *savedata_title, *savedata_directory;

    SfoReader reader;
    QString base_name = QFileInfo(path).baseName();
    QString sfo_file = QDir(path).absoluteFilePath("PARAM.SFO");

    if(!reader.load(sfo_file)) {
        return false;
    }

    title = reader.value("TITLE", base_name.toUtf8().constData());
    savedata_detail = reader.value("SAVEDATA_DETAIL", "");
    savedata_title = reader.value("SAVEDATA_TITLE", "");
    savedata_directory = reader.value("SAVEDATA_DIRECTORY", base_name.toUtf8().constData());
    date_updated = QFileInfo(sfo_file).lastModified().toTime_t();

    db.transaction();

    if((ohfi = insertObjectEntry(title, type)) == 0) {
        db.rollback();
        return false;
    }

    if(!insertSourceEntry(ohfi, path)) {
        db.rollback();
        return false;
    }

    QSqlQuery query;
    query.prepare("REPLACE INTO savedata"
        "(object_id, detail, dir_name, title, date_updated)"
        "VALUES (:object_id, :detail, :dir_name, :title, :updated)");

    query.bindValue(0, ohfi);
    query.bindValue(1, savedata_detail);
    query.bindValue(2, savedata_directory);
    query.bindValue(3, title);
    query.bindValue(4, date_updated);

    if(query.exec()) {
        db.commit();
        return true;
    } else {
        db.rollback();
        return false;
    }
}
