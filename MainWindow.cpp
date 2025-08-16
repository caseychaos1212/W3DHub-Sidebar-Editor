// MainWindow.cpp
#include "MainWindow.h"
#include "IconTileWidget.h"
#include "EditPurchaseItemDialog.h"
#include <QVBoxLayout>
#include <QScrollArea>
#include <QGridLayout>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QRegularExpression>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenuBar>
#include <QScrollArea>
#include <QListWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QSettings>
#include <QCoreApplication>
#include <algorithm>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QDirIterator>

static constexpr int kTileW = 220;
static constexpr int kTileH = 240;
static constexpr int kIcon = 200;   // actual image square inside the tile

// forward decls for helpers used by updateSelectedLevels()
static bool appendSectionBlock(QString& json, const QString& blockJson);
static QString buildSectionBlockText(int defId, const QString& name, int team, int type,
    const QString& baseIndent = QStringLiteral("\t\t\t"));
static bool appendItemToSection(QString& json, int team, int type, const PurchaseItem& it,
    const QString& listNameOptional = QString());
static QString jsonQuote(const QString& s);
// --- Level presets correlation helpers --------------------------------------

struct ParentRef {
    qint64 parentId = -1;
    QString parentName;
};

// Build (TEAM,TYPE) -> {PARENT_ID, DEF_NAME} from master file
static QMap<QPair<int, int>, ParentRef>
buildParentRefMapFromMaster(const QString& masterPath, std::function<bool(const QString&)> nameFilter = {})
{
    QMap<QPair<int, int>, ParentRef> out;

    QFile f(masterPath);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray raw = f.readAll(); f.close();

    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return out;

    const auto gs = doc.object().value(QStringLiteral("GlobalSettings")).toArray();
    for (const auto& v : gs) {
        const QJsonObject block = v.toObject();
        const QJsonObject data = block.value(QStringLiteral("FACTORY_WRAPPER")).toObject()
            .value(QStringLiteral("DATA")).toObject();

        const QJsonObject db = data.value(QStringLiteral("DEFINITION_BASE")).toObject();
        const QJsonObject ps = data.value(QStringLiteral("PURCHASE_SETTINGS_DEF_CLASS")).toObject();
        const int team = ps.value(QStringLiteral("TEAM")).toInt();
        const int type = ps.value(QStringLiteral("TYPE")).toInt();
        const qint64 pid = (qint64)db.value(QStringLiteral("ID")).toVariant().toLongLong();
        const QString pname = db.value(QStringLiteral("NAME")).toString();

        // Optional: only take certain named lists, e.g. skip "(Neutral)" if desired
        if (nameFilter && !nameFilter(pname)) continue;

        const QPair<int, int> key(team, type);
        if (!out.contains(key)) {
            out.insert(key, ParentRef{ pid, pname });
        }
        // If multiple candidates per (TEAM,TYPE) exist, first one wins.
    }
    return out;
}

static inline QString teamWord(int team) {
    return (team == 0 ? QStringLiteral("Allied") : QStringLiteral("Soviet"));
}

static inline QString typeWord(int type) {
    switch (type) {
    case 0: return QStringLiteral("Infantry");
    case 1: return QStringLiteral("Vehicles");
    case 4: return QStringLiteral("Extra Vehicles");
    case 5: return QStringLiteral("Air");
    case 7: return QStringLiteral("Extra Air");
    case 6: return QStringLiteral("Navy");
    default: return QStringLiteral("Type %1").arg(type);
    }
}

// Helper for (TEAM,TYPE) key
struct TeamType {
    int team;
    int type;
    bool operator==(const TeamType& o) const { return team == o.team && type == o.type; }
};
inline uint qHash(const TeamType& k, uint seed = 0) {
    return qHash((quint64(uint16_t(k.team) << 16) | uint16_t(k.type)), seed);
}

static inline QString normalizeTheme(QString t) {
    t = t.trimmed().toLower();
    if (t.startsWith('d') || t.contains("desert")) return "desert";
    if (t.startsWith('u') || t.contains("urban"))  return "urban";
    if (t.startsWith('s') || t.contains("snow"))   return "snow";
    return "forest";
}

static bool parseListId(const QString& listId, int& team, int& type, QString* nameOut = nullptr) {
    // Format: TEAM=%1|TYPE=%2|NAME=%3
    const auto parts = listId.split('|');
    if (parts.size() != 3) return false;
    bool ok1 = false, ok2 = false;
    team = parts[0].mid(QString("TEAM=").size()).toInt(&ok1);
    type = parts[1].mid(QString("TYPE=").size()).toInt(&ok2);
    if (!ok1 || !ok2) return false;
    if (nameOut) *nameOut = parts[2].mid(QString("NAME=").size());
    return true;
}


// Extract per-section refs (DEF_ID, DEF_NAME, TEAM, TYPE) from a level Definitions JSON string
static QVector<std::tuple<int, QString, int, int>> collectLevelDefs(const QString& defsJson) {
    QVector<std::tuple<int, QString, int, int>> out;
    QJsonParseError pe{};
    const QJsonDocument d = QJsonDocument::fromJson(defsJson.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject()) return out;

    const auto gs = d.object().value(QStringLiteral("GlobalSettings")).toArray();
    for (const auto& v : gs) {
        const auto o = v.toObject();
        const auto data = o.value(QStringLiteral("FACTORY_WRAPPER")).toObject()
            .value(QStringLiteral("DATA")).toObject();
        const auto base = data.value(QStringLiteral("DEFINITION_BASE")).toObject();
        const auto ps = data.value(QStringLiteral("PURCHASE_SETTINGS_DEF_CLASS")).toObject();

        const int defId = base.value(QStringLiteral("ID")).toInt();
        const QString name = base.value(QStringLiteral("NAME")).toString();
        const int team = ps.value(QStringLiteral("TEAM")).toInt();
        const int type = ps.value(QStringLiteral("TYPE")).toInt();

        if (defId != 0)
            out.push_back({ defId, name, team, type });
    }
    return out;
}

// Convert {team,type} -> parentId map into the per-level Presets/GlobalSettings.json
static bool writeLevelPresetsGlobalSettings(const QString& levelEditRootPath,
    const QString& level,
    const QString& levelDefinitionsJson,
    const QHash<TeamType, int>& parentByTT)
{
    // 1) Gather sections from the freshly written *Definitions* JSON
    const auto defs = collectLevelDefs(levelDefinitionsJson); // (defId, defName, team, type)

    // 2) Build the file text manually so SCHEMA_VERSION and VERSION stay at the top
    QStringList elemLines;
    elemLines.reserve(defs.size());

    for (const auto& tup : defs) {
        const int defId = std::get<0>(tup);
        const QString defName = std::get<1>(tup);   // whatever you set in Definitions/DEFINITION_BASE.NAME
        const int team = std::get<2>(tup);
        const int type = std::get<3>(tup);
        const int parentId = parentByTT.value(TeamType{ team, type }, 0);

        QString e;
        e += "\t\t{\n";
        e += "\t\t\t\"DEF_ID\": " + QString::number(defId) + ",\n";
        e += "\t\t\t\"DEF_NAME\": " + jsonQuote(defName) + ",\n";
        e += "\t\t\t\"IS_TEMP\": true,\n";
        e += "\t\t\t\"PARENT_ID\": " + QString::number(parentId) + "\n";
        e += "\t\t}";
        elemLines << e;
    }

    QString out;
    out += "{\n";
    out += "\t\"SCHEMA_VERSION\": 1,\n";
    out += "\t\"VERSION\": 13361,\n";
    out += "\t\"GlobalSettings\": [\n";
    out += elemLines.join(",\n");
    out += "\n\t]\n";
    out += "}\n";

    // 3) Write to /Presets
    const QString outPath =
        QStringLiteral("%1/Database/Levels/%2/Presets/GlobalSettings.json")
        .arg(levelEditRootPath, level);

    QDir().mkpath(QFileInfo(outPath).path());
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(out.toUtf8());
    f.close();
    return true;
}
// --- adapter: turn QMap<QPair<int,int>, ParentRef> into QHash<TeamType, int> ---
static QHash<TeamType, int>
flattenParentMap(const QMap<QPair<int, int>, ParentRef>& src)
{
    QHash<TeamType, int> out;
    out.reserve(src.size());
    for (auto it = src.cbegin(); it != src.cend(); ++it) {
        const int team = it.key().first;
        const int type = it.key().second;
        // NOTE: if your ParentRef uses a different member name than 'parentId',
        // change the line below accordingly (e.g., it.value().id).
        out.insert(TeamType{ team, type }, it.value().parentId);
    }
    return out;
}
static QHash<TeamType, int> buildParentRefMapFromMasterTT(const QString& masterPath) {
    QHash<TeamType, int> map;
    QJsonDocument d; {
        QFile f(masterPath);
        if (!f.open(QIODevice::ReadOnly)) return map;
        QJsonParseError pe{};
        d = QJsonDocument::fromJson(f.readAll(), &pe);
        f.close();
        if (pe.error != QJsonParseError::NoError || !d.isObject()) return map;
    }
    const auto gs = d.object().value(QStringLiteral("GlobalSettings")).toArray();
    for (const auto& v : gs) {
        const auto o = v.toObject();
        const auto data = o.value(QStringLiteral("FACTORY_WRAPPER")).toObject()
            .value(QStringLiteral("DATA")).toObject();
        const auto base = data.value(QStringLiteral("DEFINITION_BASE")).toObject();
        const auto ps = data.value(QStringLiteral("PURCHASE_SETTINGS_DEF_CLASS")).toObject();

        const int parentId = base.value(QStringLiteral("ID")).toInt();
        const int team = ps.value(QStringLiteral("TEAM")).toInt();
        const int type = ps.value(QStringLiteral("TYPE")).toInt();

        // Keep first seen per (TEAM,TYPE); if you have multiple variants per (TEAM,TYPE) adjust here.
        TeamType key{ team, type };
        if (!map.contains(key)) map.insert(key, parentId);
    }
    return map;
}

// Return the indent to use for* elements* inside a named array.
// Example: for "GlobalSettings": [ ... ] it returns (indent_of_']' + "\t")
static QString arrayElemIndent(const QString& json, const QString& arrayKey)
{
    QRegularExpression re(QStringLiteral("\"") + QRegularExpression::escape(arrayKey) + QStringLiteral("\"\\s*:\\s*\\["));
    auto m = re.match(json);
    if (!m.hasMatch())
        return QStringLiteral("\t\t"); // safe fallback: two tabs

    // Walk to the matching ']'
    int i = m.capturedEnd(), depth = 1;
    while (i < json.size() && depth > 0) {
        const QChar ch = json.at(i++);
        if (ch == '[') ++depth;
        else if (ch == ']') --depth;
        else if (ch == '\"') {
            while (i < json.size()) {
                if (json.at(i) == '\\') { i += 2; continue; }
                if (json.at(i) == '\"') { ++i; break; }
                ++i;
            }
        }
    }
    const int bracketPos = i - 1;               // position of ']'
    const int lineStart = json.lastIndexOf('\n', bracketPos);

    QString closingIndent;
    if (lineStart >= 0) {
        int j = lineStart + 1;
        while (j < json.size() && (json.at(j) == ' ' || json.at(j) == '\t')) {
            closingIndent += json.at(j); ++j;
        }
    }
    // Elements should be one level deeper than the closing bracket line.
    return closingIndent + QStringLiteral("\t");
}

static QString detectGlobalSettingsElemIndent(const QString& json) {
    QRegularExpression re(QStringLiteral("\"GlobalSettings\"\\s*:\\s*\\["));
    auto m = re.match(json);
    if (!m.hasMatch()) return QStringLiteral("\t\t"); // safe fallback

    const int bracketPos = m.capturedEnd();
    int lineStart = json.lastIndexOf('\n', bracketPos);
    QString baseIndent;
    if (lineStart >= 0) {
        int i = lineStart + 1;
        while (i < json.size() && (json.at(i) == ' ' || json.at(i) == '\t')) {
            baseIndent += json.at(i);
            ++i;
        }
    }
    return baseIndent + QStringLiteral("\t");
}

struct PatchOptions {
    bool coreFields = true;   // cost/tech/limits/factory flags
    bool textures = false;  // base TEXTURE
    bool altArrays = false;  // ALT_PRESETIDS / ALT_TEXTURES
};



// ===== Surgical JSON patch helpers =====
static inline QString canonEmpty(const QString& s) {
    return s.trimmed().isEmpty() ? QString() : s;
}

// ===== Surgical JSON patch helpers
static QString jsonQuote(const QString& s) {
    QString out = canonEmpty(s);     // <- normalize here
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    return "\"" + out + "\"";
}

// Find `"KEY": <value>` and return positions of the <value>
static bool findKeyValueSpan(const QString& obj, const QString& key, int& vStart, int& vLen) {
    QRegularExpression re(
        QString::fromLatin1(R"xx(")xx") +
        QRegularExpression::escape(key) +
        QString::fromLatin1(R"xx("\s*:\s*(?P<val>(?:"(?:\\.|[^"])*")|(?:\[[^\]]*\])|[^,\}\n]+))xx"));
    auto m = re.match(obj);
    if (!m.hasMatch()) return false;
    vStart = m.capturedStart("val");
    vLen = m.capturedLength("val");
    return true;
}
// Re-indent ALT_* arrays inside a single object string (the one we just inserted).
static void normalizeTripleArraysIndent(QString& obj) {
    auto reformat = [&](const QString& key, bool stringy) {
        int s = 0, l = 0; if (!findKeyValueSpan(obj, key, s, l)) return;

        // Indent: closing ']' aligns with the key; values one extra tab in.
        QString keyIndent;
        int ln = obj.lastIndexOf('\n', s);
        for (int i = ln + 1; i < obj.size() && (obj[i] == ' ' || obj[i] == '\t'); ++i)
            keyIndent += obj[i];
        const QString valIndent = keyIndent + "\t";

        const QString raw = obj.mid(s, l).trimmed();
        QJsonParseError pe{};
        QJsonDocument d = QJsonDocument::fromJson(raw.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !d.isArray()) return;

        const QJsonArray a = d.array();
        auto grab = [&](int i) -> QString {
            QJsonValue v = (i >= 0 && i < a.size()) ? a.at(i) : QJsonValue();
            if (stringy) {
                return jsonQuote(canonEmpty(v.toString()));
            }
            else {
                // Keep integers looking like integers
                qlonglong iv = v.isDouble() ? v.toVariant().toLongLong() : 0;
                return QString::number(iv);
            }
            };

        const QString out =
            "[\n" + valIndent + grab(0) + ",\n" +
            valIndent + grab(1) + ",\n" +
            valIndent + grab(2) + "\n" +
            keyIndent + "]";

        obj.replace(s, l, out);
        };

    reformat(QStringLiteral("ALT_PRESETIDS"), false);
    reformat(QStringLiteral("ALT_TEXTURES"), true);
}

static QHash<QString, int> gParentIdByListId;
static QHash<QString, QString> gNameByListId;

// Replace a simple (number/string/bool) value literal
static bool replaceKeyLiteral(QString& obj, const QString& key, const QString& valueLiteral) {
    int s = 0, l = 0; if (!findKeyValueSpan(obj, key, s, l)) return false;
    obj.replace(s, l, valueLiteral);
    return true;
}

// Replace the array *elements* (ints) with one extra level of indent if multiline.
static bool replaceArrayIntsPreserving(QString& obj, const QString& key, const QVector<int>& xs) {
    int s = 0, l = 0; if (!findKeyValueSpan(obj, key, s, l)) return false;
    const QString val = obj.mid(s, l);
    const QString trimmed = val.trimmed();

    auto oneline = [&](int a, int b, int c) {
        return QString("[%1, %2, %3]").arg(a).arg(b).arg(c);
        };

    if (!trimmed.startsWith('[') || !trimmed.endsWith(']')) {
        obj.replace(s, l, oneline(xs.value(0, 0), xs.value(1, 0), xs.value(2, 0)));
        return true;
    }

    if (val.contains('\n')) {
        QRegularExpression firstIndentRe(R"(\[\s*\n([ \t]+))");
        QRegularExpression endIndentRe(R"(\n([ \t]*)\])");
        QString elemIndent, endIndent;
        if (auto m = firstIndentRe.match(val); m.hasMatch()) elemIndent = m.captured(1);
        if (auto m = endIndentRe.match(val); m.hasMatch())   endIndent = m.captured(1);

        const QString out =
            "[\n" + elemIndent + QString::number(xs.value(0, 0)) + ",\n" +
            elemIndent + QString::number(xs.value(1, 0)) + ",\n" +
            elemIndent + QString::number(xs.value(2, 0)) + "\n" +
            endIndent + "]";
        obj.replace(s, l, out);
        return true;
    }

    obj.replace(s, l, oneline(xs.value(0, 0), xs.value(1, 0), xs.value(2, 0)));
    return true;
}

//    // Single-line: keep single-line.
 //   const QString out = QString("[%1, %2, %3]").arg(xs.value(0, 0)).arg(xs.value(1, 0)).arg(xs.value(2, 0));
//    obj.replace(s, l, out);
 //   return true;
//}

// Replace the array *elements* (strings) with one extra level of indent if multiline.
static bool replaceArrayStringsPreserving(QString& obj, const QString& key, const QVector<QString>& xsIn) {
    QVector<QString> xs = xsIn;
    for (QString& v : xs) v = canonEmpty(v);

    int s = 0, l = 0; if (!findKeyValueSpan(obj, key, s, l)) return false;
    const QString val = obj.mid(s, l);
    const QString trimmed = val.trimmed();

    auto q = [](const QString& s) { return jsonQuote(canonEmpty(s)); };
    auto oneline = [&](const QString& a, const QString& b, const QString& c) {
        return "[" + q(a) + ", " + q(b) + ", " + q(c) + "]";
        };

    if (!trimmed.startsWith('[') || !trimmed.endsWith(']')) {
        obj.replace(s, l, oneline(xs.value(0), xs.value(1), xs.value(2)));
        return true;
    }

    if (val.contains('\n')) {
        QRegularExpression firstIndentRe(R"(\[\s*\n([ \t]+))");
        QRegularExpression endIndentRe(R"(\n([ \t]*)\])");
        QString elemIndent, endIndent;
        if (auto m = firstIndentRe.match(val); m.hasMatch()) elemIndent = m.captured(1);
        if (auto m = endIndentRe.match(val); m.hasMatch())   endIndent = m.captured(1);

        const QString out =
            "[\n" + elemIndent + q(xs.value(0)) + ",\n" +
            elemIndent + q(xs.value(1)) + ",\n" +
            elemIndent + q(xs.value(2)) + "\n" +
            endIndent + "]";
        obj.replace(s, l, out);
        return true;
    }

    obj.replace(s, l, oneline(xs.value(0), xs.value(1), xs.value(2)));
    return true;
}

    // Single-line: keep single-line.
 //   const QString out = "[" + jsonQuote(xs.value(0)) + ", " + jsonQuote(xs.value(1)) + ", " + jsonQuote(xs.value(2)) + "]";
 //   obj.replace(s, l, out);
//    return true;
//}






static QString jsonArrayInts(const QVector<int>& xs, int n = 3) {
    QStringList parts;
    for (int i = 0; i < n; ++i) parts << QString::number(xs.value(i, 0));
    return "[" + parts.join(", ") + "]";
}
static QString jsonArrayStrings(const QVector<QString>& xs, int n = 3) {
    QStringList parts;
    for (int i = 0; i < n; ++i) parts << jsonQuote(xs.value(i, ""));
    return "[" + parts.join(", ") + "]";
}


static bool patchPurchaseItemInText(QString& json,
    int team, int type, int presetId,
    const PurchaseItem& src,
    const QString& /*listNameOptional*/, // NAME lives in DEFINITION_BASE; we ignore here
    const PatchOptions& opt,
    bool* outFound = nullptr,
    int* inoutPos = nullptr)
{
    if (outFound) *outFound = false;

    int pos = inoutPos ? *inoutPos : 0;
    int arrStart = -1, arrEnd = -1, objClose = -1;

    // Walk PS_DEF_CLASS blocks forward from 'pos'
    while (true) {
        const int anchor = json.indexOf(QStringLiteral("\"PURCHASE_SETTINGS_DEF_CLASS\""), pos);
        if (anchor < 0) {
            if (inoutPos) *inoutPos = json.size();
            return false; // nothing more to scan
        }

        // Match braces for this PS_DEF_CLASS object
        int objOpen = json.indexOf('{', anchor);
        if (objOpen < 0) { if (inoutPos) *inoutPos = anchor + 1; return false; }

        int i = objOpen + 1, depth = 1;
        while (i < json.size() && depth > 0) {
            const QChar ch = json.at(i++);
            if (ch == '\"') {
                while (i < json.size()) {
                    if (json.at(i) == '\\') { i += 2; continue; }
                    if (json.at(i) == '\"') { ++i; break; }
                    ++i;
                }
            }
            else if (ch == '{') {
                ++depth;
            }
            else if (ch == '}') {
                --depth;
            }
        }
        if (depth != 0) { if (inoutPos) *inoutPos = json.size(); return false; }
        objClose = i; // one past '}'

        const QString section = json.mid(objOpen, objClose - objOpen);
        const bool teamOk = section.contains(QRegularExpression(QStringLiteral("\"TEAM\"\\s*:\\s*%1").arg(team)));
        const bool typeOk = section.contains(QRegularExpression(QStringLiteral("\"TYPE\"\\s*:\\s*%1").arg(type)));
        if (!teamOk || !typeOk) { pos = objClose; continue; }

        // Find PURCHASE_ITEMS array in this section
        QRegularExpression itemsRe(QStringLiteral("\"PURCHASE_ITEMS\"\\s*:\\s*\\["));
        auto m = itemsRe.match(section);
        if (!m.hasMatch()) { pos = objClose; continue; }

        arrStart = objOpen + m.capturedEnd();

        int j = arrStart, bDepth = 1;
        while (j < json.size() && bDepth > 0) {
            const QChar ch2 = json.at(j++);
            if (ch2 == '\"') {
                while (j < json.size()) {
                    if (json.at(j) == '\\') { j += 2; continue; }
                    if (json.at(j) == '\"') { ++j; break; }
                    ++j;
                }
            }
            else if (ch2 == '[') {
                ++bDepth;
            }
            else if (ch2 == ']') {
                --bDepth;
            }
        }
        if (bDepth != 0) { pos = objClose; continue; }
        arrEnd = j - 1;

        // Search this array for our PRESET_ID
        QString arr = json.mid(arrStart, arrEnd - arrStart);
        QRegularExpression objRe(
            QString::fromLatin1(R"(\{[^{}]*?"PRESET_ID"\s*:\s*)") +
            QString::number(presetId) +
            QString::fromLatin1(R"([^{}]*?\})"));
        auto objMatch = objRe.match(arr);
        if (!objMatch.hasMatch()) {
            // No such PRESET here; move to next section
            pos = objClose;
            continue;
        }

        if (outFound) *outFound = true;

        int objStart = objMatch.capturedStart();
        int objEnd = objMatch.capturedEnd();
        QString obj = arr.mid(objStart, objEnd - objStart);

        // Detect if anything needs to change
        bool changed = true; // default; set precisely if we can parse JSON
        {
            QJsonParseError pe{};
            const QJsonDocument d = QJsonDocument::fromJson(obj.toUtf8(), &pe);
            if (pe.error == QJsonParseError::NoError && d.isObject()) {
                const QJsonObject e = d.object();
                auto qv = [&](const char* k) { return e.value(QLatin1String(k)); };
                auto sameInt = [&](const char* k, int v) { return qv(k).toInt() == v; };
                auto sameBool = [&](const char* k, bool v) { return qv(k).toBool() == v; };
                auto sameStr = [&](const char* k, const QString& v) {
                    return qv(k).toString().trimmed() == canonEmpty(v);
                    };
                auto sameArrI3 = [&](const char* k, const QVector<int>& v) {
                    const auto a = qv(k).toArray();
                    return a.size() >= 3 &&
                        a[0].toInt() == v.value(0, 0) &&
                        a[1].toInt() == v.value(1, 0) &&
                        a[2].toInt() == v.value(2, 0);
                    };
                auto sameArrS3 = [&](const char* k, const QVector<QString>& v) {
                    const auto a = qv(k).toArray();
                    return a.size() >= 3 &&
                        a[0].toString() == canonEmpty(v.value(0)) &&
                        a[1].toString() == canonEmpty(v.value(1)) &&
                        a[2].toString() == canonEmpty(v.value(2));
                    };

                changed = false;
                if (opt.coreFields) {
                    changed = changed
                        || !sameInt("COST", src.cost)
                        || !sameInt("TECH_LEVEL", src.techLevel)
                        || !sameInt("SPECIAL_TECH_NUMBER", src.specialTechNumber)
                        || !sameInt("UNIT_LIMIT", src.unitLimit)
                        || !sameInt("FACTORY", src.factory)
                        || !sameInt("TECH_BUILDING", src.techBuilding)
                        || !sameBool("FACTORY_NOT_REQUIRED", src.factoryNotRequired);
                }
                if (opt.textures)  changed = changed || !sameStr("TEXTURE", src.texture);
                if (opt.altArrays) changed = changed
                    || !sameArrI3("ALT_PRESETIDS", src.altPresetIds)
                    || !sameArrS3("ALT_TEXTURES", src.altTextures);
            }
        }

        if (!changed) {
            // Found the item but no update needed. Advance scan past this section.
            if (inoutPos) *inoutPos = objClose;
            return false;
        }

        // Apply the updates
        if (opt.coreFields) {
            replaceKeyLiteral(obj, "COST", QString::number(src.cost));
            replaceKeyLiteral(obj, "TECH_LEVEL", QString::number(src.techLevel));
            replaceKeyLiteral(obj, "SPECIAL_TECH_NUMBER", QString::number(src.specialTechNumber));
            replaceKeyLiteral(obj, "UNIT_LIMIT", QString::number(src.unitLimit));
            replaceKeyLiteral(obj, "FACTORY", QString::number(src.factory));
            replaceKeyLiteral(obj, "TECH_BUILDING", QString::number(src.techBuilding));
            replaceKeyLiteral(obj, "FACTORY_NOT_REQUIRED", (src.factoryNotRequired ? "true" : "false"));
        }
        if (opt.textures)  replaceKeyLiteral(obj, "TEXTURE", jsonQuote(src.texture));
        if (opt.altArrays) {
            replaceArrayIntsPreserving(obj, "ALT_PRESETIDS", src.altPresetIds);
            replaceArrayStringsPreserving(obj, "ALT_TEXTURES", src.altTextures);
        }

        // Splice back
        arr.replace(objStart, objEnd - objStart, obj);
        json.replace(arrStart, arrEnd - arrStart, arr);

        // Advance scan beyond this section
        if (inoutPos) *inoutPos = objClose;
        return true; // changed this occurrence
    }
}


// Build a canonical key for a unit group: sorted unique of base + alt preset IDs
static QString canonicalPresetKey(const PurchaseItem& it) {
    QVector<int> ids;
    ids.reserve(1 + it.altPresetIds.size());
    ids.push_back(it.presetId);
    for (int id : it.altPresetIds) if (id != 0) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    QString key; key.reserve(ids.size() * 12);
    for (int id : ids) { key += QString::number(id); key += '_'; }
    return key;
}

static QString canonicalPresetKey(const QJsonObject& e) {
    QVector<int> ids;
    ids.reserve(4);
    ids.push_back(e.value("PRESET_ID").toInt());
    for (const auto& v : e.value("ALT_PRESETIDS").toArray())
        if (int id = v.toInt(); id != 0) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    QString key; key.reserve(ids.size() * 12);
    for (int id : ids) { key += QString::number(id); key += '_'; }
    return key;
}

QHash<QString, PurchaseItem> MainWindow::buildEditedMapFromTabs() const {
    QHash<QString, PurchaseItem> map;
    for (auto it = categorizedLists.cbegin(); it != categorizedLists.cend(); ++it) {
        for (const PurchaseItem& item : it.value()) {
            const QString key = canonicalPresetKey(item);
            map.insert(key, item);
        }
    }
    return map;
}
bool MainWindow::loadJsonFromFile(const QString& path, QJsonDocument& outDoc) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray raw = f.readAll(); f.close();

    QJsonParseError err{};
    outDoc = QJsonDocument::fromJson(raw, &err);
    if (err.error == QJsonParseError::NoError) return true;

    // fallback encoding shim you already use
    const QString asLatin1 = QString::fromLatin1(raw);
    outDoc = QJsonDocument::fromJson(asLatin1.toUtf8(), &err);
    return err.error == QJsonParseError::NoError;
}

bool MainWindow::writeJsonToFile(const QString& path, const QJsonDocument& doc) const {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

QJsonObject MainWindow::itemToJson(const PurchaseItem& item) const {
    QJsonObject o;
    o["COST"] = item.cost;
    o["PRESET_ID"] = item.presetId;
    o["STRING_ID"] = item.stringId;
    o["TEXTURE"] = item.texture;
    o["TECH_LEVEL"] = item.techLevel;
    o["SPECIAL_TECH_NUMBER"] = item.specialTechNumber;
    o["UNIT_LIMIT"] = item.unitLimit;
    o["FACTORY"] = item.factory;
    o["TECH_BUILDING"] = item.techBuilding;
    o["FACTORY_NOT_REQUIRED"] = item.factoryNotRequired;

    QJsonArray altIds, altTex;
    for (int i = 0; i < 3; ++i) {
        altIds.append(item.altPresetIds.value(i, 0));
        altTex.append(item.altTextures.value(i, ""));
    }
    o["ALT_PRESETIDS"] = altIds;
    o["ALT_TEXTURES"] = altTex;
    return o;
}

// Merge b into a (union textures/alts; min cost/tech as a sane default)
static void mergePurchaseItem(PurchaseItem& a, const PurchaseItem& b) {
    // cost/tech: keep the lowest tech and lowest cost (tweak if you want different policy)
    a.techLevel = std::min(a.techLevel, b.techLevel);
    a.cost = std::min(a.cost, b.cost);

    // texture pool: a.base + a.alts + b.base + b.alts -> unique, non-empty
    QStringList pool;
    auto addTex = [&](const QString& t) { const QString n = t.trimmed(); if (!n.isEmpty()) pool << n; };
    addTex(a.texture);
    for (const auto& t : a.altTextures) addTex(t);
    addTex(b.texture);
    for (const auto& t : b.altTextures) addTex(t);

    QSet<QString> seen;
    QStringList uniq;
    for (const auto& t : pool) if (!seen.contains(t)) { seen.insert(t); uniq << t; }

    // choose first as base, next up to 3 as alts
    if (!uniq.isEmpty()) {
        a.texture = uniq.front();
        uniq.pop_front();
    }
    a.altTextures = QVector<QString>::fromList(uniq.mid(0, 3));
    while (a.altTextures.size() < 3) a.altTextures << "";

    // alt preset IDs: union of both (non-zero), but keep only up to 3 in the JSON slots
    QVector<int> ids;
    ids << a.presetId;
    for (int id : a.altPresetIds) if (id) ids << id;
    ids << b.presetId;
    for (int id : b.altPresetIds) if (id) ids << id;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::remove(ids.begin(), ids.end(), 0), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    // keep a.presetId as-is; fill the rest from union (excluding base)
    QVector<int> rest = ids;
    rest.erase(std::remove(rest.begin(), rest.end(), a.presetId), rest.end());
    a.altPresetIds = rest.mid(0, 3);
    while (a.altPresetIds.size() < 3) a.altPresetIds << 0;
}

// Dedupe items in a tab (label) by canonical preset set
static void dedupeWithinLabel(QVector<PurchaseItem>& list) {
    QMap<QString, PurchaseItem> byKey;
    for (const PurchaseItem& it : list) {
        if (it.presetId == 0) continue; // skip placeholders
        const QString key = canonicalPresetKey(it);
        auto found = byKey.find(key);
        if (found == byKey.end()) {
            PurchaseItem base = it;
            // normalize textures of the first item
           // mergePurchaseItem(base, PurchaseItem{});
            byKey.insert(key, base);
        }
        else {
            PurchaseItem merged = found.value();
            mergePurchaseItem(merged, it);
            found.value() = merged;
        }
    }

    list = byKey.values().toVector();

    // Optional: stable sort (e.g., by tech then cost then preset)
    std::stable_sort(list.begin(), list.end(), [](const PurchaseItem& a, const PurchaseItem& b) {
        if (a.techLevel != b.techLevel) return a.techLevel < b.techLevel;
        if (a.cost != b.cost)      return a.cost < b.cost;
        return a.presetId < b.presetId;
        });
}

QString levelEditRootPath;
QMap<QString, QString> mapCamoAssignments;



void MainWindow::showMapTheaterWidget() {
    if (levelEditRootPath.isEmpty()) {
        levelEditRootPath = QFileDialog::getExistingDirectory(this, "Select LevelEdit Folder", QDir::homePath());
        if (levelEditRootPath.isEmpty()) return;
        QSettings settings("SidebarTool", "SidebarEditor");
        settings.setValue("LevelEditRoot", levelEditRootPath);
    }

    QString basePath = levelEditRootPath + "/Database/Levels";
    QDir baseDir(basePath);
    QStringList mapDirs = baseDir.entryList(QStringList("RA_*"), QDir::Dirs | QDir::NoDotAndDotDot);

    QDialog* mapDialog = new QDialog(this);
    mapDialog->setWindowTitle("Assign Theater Per Map");
    mapDialog->resize(700, 500);

    QHBoxLayout* mainLayout = new QHBoxLayout(mapDialog);

    QListWidget* mapList = new QListWidget;
    mapList->setSelectionMode(QAbstractItemView::MultiSelection);

    QFile profileFile("camo_profile.json");
    QJsonObject saved;
    if (profileFile.open(QIODevice::ReadOnly)) {
        saved = QJsonDocument::fromJson(profileFile.readAll()).object();
        profileFile.close();
    }

    for (const QString& map : mapDirs) {
        QString theme = saved.value(map).toString();
        if (!theme.isEmpty()) {
            mapCamoAssignments[map] = theme;
            mapList->addItem(map + " (" + theme + ")");
        }
        else {
            mapList->addItem(map);
        }
    }

    QVBoxLayout* controlLayout = new QVBoxLayout;
    QLabel* camoLabel = new QLabel("Select Camouflage:");
    QComboBox* camoCombo = new QComboBox;
    camoCombo->addItem("forest");
    camoCombo->addItem("desert");
    camoCombo->addItem("urban");
    camoCombo->addItem("snow");

    QPushButton* assignBtn = new QPushButton("Apply to Selected");
    connect(assignBtn, &QPushButton::clicked, [=]() {
        for (QListWidgetItem* item : mapList->selectedItems()) {
            QString rawMap = item->text().split(" ").first();
            QString selectedTheme = camoCombo->currentText();
            mapCamoAssignments[rawMap] = selectedTheme;
            item->setText(rawMap + " (" + selectedTheme + ")");
        }
        });

    QPushButton* saveProfileBtn = new QPushButton("Save Profile");
    connect(saveProfileBtn, &QPushButton::clicked, [=]() {
        QJsonObject profile;
        for (auto it = mapCamoAssignments.begin(); it != mapCamoAssignments.end(); ++it) {
            profile[it.key()] = it.value();
        }
        QFile out("camo_profile.json");
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(profile).toJson(QJsonDocument::Indented));
            out.close();
        }
        });

    QPushButton* exportAllBtn = new QPushButton("Export All to GlobalSettings.json");
    connect(exportAllBtn, &QPushButton::clicked, this, &MainWindow::exportAllMapJsons);
    QPushButton* applyToFilesBtn = new QPushButton("Apply camo to level files");
    connect(applyToFilesBtn, &QPushButton::clicked, [=]() {
        QStringList changed, skipped;

        // Run on the *selected* items in the list; if none selected, run on all with assignments
        QList<QListWidgetItem*> targets = mapList->selectedItems();
        if (targets.isEmpty()) {
            for (auto it = mapCamoAssignments.cbegin(); it != mapCamoAssignments.cend(); ++it) {
                const QString& lvl = it.key();
                const QString& thm = it.value();
                if (thm.isEmpty()) { skipped << lvl; continue; }
                if (reorderCamoInLevelFile(lvl, thm)) changed << lvl; else skipped << lvl;
            }
        }
        else {
            for (QListWidgetItem* item : targets) {
                const QString lvl = item->text().split(" ").first();
                const QString thm = mapCamoAssignments.value(lvl);
                if (thm.isEmpty()) { skipped << lvl; continue; }
                if (reorderCamoInLevelFile(lvl, thm)) changed << lvl; else skipped << lvl;
            }
        }

        QMessageBox::information(mapDialog, "Camo reorder",
            QString("Updated %1 level%2.\nSkipped: %3")
            .arg(changed.size())
            .arg(changed.size() == 1 ? "" : "s")
            .arg(skipped.isEmpty() ? "None" : skipped.join(", ")));
        });
    controlLayout->addWidget(camoLabel);
    controlLayout->addWidget(camoCombo);
    controlLayout->addWidget(assignBtn);
    controlLayout->addStretch();
    controlLayout->addWidget(saveProfileBtn);
 //   controlLayout->addWidget(exportAllBtn);
    controlLayout->addWidget(applyToFilesBtn);

    mainLayout->addWidget(mapList);
    mainLayout->addLayout(controlLayout);

    mapDialog->setLayout(mainLayout);
    mapDialog->exec();
}



MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    QSettings* settings = new QSettings("SidebarTool", "SidebarEditor", this);
    levelEditRootPath = settings->value("LevelEditRoot").toString();
    if (levelEditRootPath.isEmpty()) {
        levelEditRootPath = QFileDialog::getExistingDirectory(this, "Select LevelEdit Folder", QDir::homePath());
        if (!levelEditRootPath.isEmpty())
            settings->setValue("LevelEditRoot", levelEditRootPath);
    }

    tabWidget = new QTabWidget(this);
    setCentralWidget(tabWidget);

    loadMasterJson("GlobalSettings.json");
    rebuildFromSelection();               // < build from chosen lists
    //applyCamoDefaults("forest");          

    // Export button
   // auto* exportBtn = new QPushButton("Export JSON", this);
   // connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportMapJson);
   // tabWidget->setCornerWidget(exportBtn, Qt::TopRightCorner);

    // Menu
    QMenu* toolsMenu = menuBar()->addMenu("Tools");
    toolsMenu->addAction("Choose Source Lists", this, &MainWindow::showListPickerDialog);
    toolsMenu->addAction("Retarget LevelEdit Folder", [=]() {
        QString newPath = QFileDialog::getExistingDirectory(this, "Select LevelEdit Folder", levelEditRootPath);
        if (!newPath.isEmpty()) {
            levelEditRootPath = newPath;
            QSettings("SidebarTool", "SidebarEditor").setValue("LevelEditRoot", levelEditRootPath);
            loadMasterJson("GlobalSettings.json");
            rebuildFromSelection();
        }

        });
    toolsMenu->addSeparator();
    toolsMenu->addAction("Update Global from Current Tabs (selected lists)", this, &MainWindow::updateMasterFromTabs);
    toolsMenu->addAction("Update Levels from Current Tabs", this, &MainWindow::updateSelectedLevels);
    toolsMenu->addAction("Propagate changes to All", this, &MainWindow::updateMasterFromTabsAllLists);
    toolsMenu->addAction("Assign Camouflage to Levels", this, &MainWindow::showMapTheaterWidget);
}



// small helpers
static inline QString normTex(const QString& s) { return s.trimmed(); }

static void mergeItem(PurchaseItem& dst, const PurchaseItem& src) {
    // Prefer a non-empty base texture; otherwise keep existing
    if (dst.texture.trimmed().isEmpty() && !src.texture.trimmed().isEmpty())
        dst.texture = src.texture.trimmed();

    // Merge alts (dst.texture + dst.altTextures + src.texture + src.altTextures) -> unique non-empty
    QStringList pool;
    pool << dst.texture;
    for (auto& t : dst.altTextures) pool << t;
    pool << src.texture;
    for (auto& t : src.altTextures) pool << t;

    // unique, non-empty while preserving first occurrence
    QSet<QString> seen;
    QStringList uniq;
    for (auto& t : pool) {
        const QString n = normTex(t);
        if (n.isEmpty()) continue;
        if (seen.contains(n)) continue;
        seen.insert(n);
        uniq << n;
    }

    // First is base, next up to 3 are alts
    if (!uniq.isEmpty()) {
        dst.texture = uniq.front();
        uniq.pop_front();
    }
    dst.altTextures = QVector<QString>::fromList(uniq.mid(0, 3));
    while (dst.altTextures.size() < 3) dst.altTextures.push_back("");
}

void MainWindow::loadMasterJson(const QString& relativePath) {
    allLists.clear();

    const QString fullPath = levelEditRootPath + "/Database/Global/Definitions/" + relativePath;
    QFile f(fullPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open master JSON:" << fullPath;
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();

    
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(raw, &err);

   
    if (err.error != QJsonParseError::NoError) {
        const QString asLatin1 = QString::fromLatin1(raw);     
        const QByteArray utf8 = asLatin1.toUtf8();
        err = {};
        doc = QJsonDocument::fromJson(utf8, &err);

        if (err.error != QJsonParseError::NoError) {
            qWarning() << "JSON parse failed (UTF8 and Latin1):" << err.errorString();
            return;
        }
    }

    const auto gs = doc.object().value("GlobalSettings").toArray();
    for (const QJsonValue& v : gs) {
        const QJsonObject data = v.toObject()["FACTORY_WRAPPER"].toObject()["DATA"].toObject();
        const QJsonObject def = data["DEFINITION_BASE"].toObject();
        const QJsonObject ps = data["PURCHASE_SETTINGS_DEF_CLASS"].toObject();

        const QString defName = def["NAME"].toString();
        if (defName.contains("(Neutral)", Qt::CaseInsensitive)) continue; // skip Vehicles (Neutral), etc.

        int team = ps["TEAM"].toInt();
        int type = ps["TYPE"].toInt();
        if (type == 2 || type == 3) continue; // Equipment / Ignore

        PurchaseList pl;
        pl.name = defName;
        pl.team = team;
        pl.type = type;
        pl.id = QString("TEAM=%1|TYPE=%2|NAME=%3").arg(team).arg(type).arg(defName);

        const auto items = ps["PURCHASE_ITEMS"].toArray();
        for (const auto& iv : items) {
            const auto e = iv.toObject();
            const QString tex = e["TEXTURE"].toString().trimmed();
            if (tex.isEmpty()) continue; // drop blanks

            PurchaseItem it;
            it.cost = e["COST"].toInt();
            it.presetId = e["PRESET_ID"].toInt();
            it.stringId = e["STRING_ID"].toInt();
            it.texture = tex;
            it.techLevel = e["TECH_LEVEL"].toInt();
            it.specialTechNumber = e["SPECIAL_TECH_NUMBER"].toInt();
            it.unitLimit = e["UNIT_LIMIT"].toInt();
            it.factory = e["FACTORY"].toInt();
            it.techBuilding = e["TECH_BUILDING"].toInt();
            it.factoryNotRequired = e["FACTORY_NOT_REQUIRED"].toBool();
            it.team = team;
            it.type = type;
            for (const auto& ap : e["ALT_PRESETIDS"].toArray()) it.altPresetIds.append(ap.toInt());
            for (const auto& at : e["ALT_TEXTURES"].toArray())  it.altTextures.append(at.toString());
            gParentIdByListId[pl.id] = def["ID"].toInt();   // DEFINITION_BASE.ID of the source list
            gNameByListId[pl.id] = defName;              // DEFINITION_BASE.NAME of the source list
            pl.items.append(it);

        }
        if (!pl.items.isEmpty()) allLists.append(pl);
    }

    // Restore selection (do NOT auto-select on first run)
    QSettings s("SidebarTool", "SidebarEditor");
    const QStringList saved = s.value("SelectedLists").toStringList();
    selectedListIds = QSet<QString>(saved.cbegin(), saved.cend());
    // If empty, we leave it empty on purpose.
    
}


QString MainWindow::typeTeamToLabel(int type, int team) {
    QString teamStr = (team == 0 ? "Allied" : "Soviet");
    switch (type) {
    case 0: return teamStr + " Infantry";
    case 1:
    case 4: return teamStr + " Vehicles";
    case 5:
    case 7: return teamStr + " Air";
    case 6: return teamStr + " Navy";
    }
    return "Other";
}

void MainWindow::buildTabs() {
    for (auto it = categorizedLists.begin(); it != categorizedLists.end(); ++it) {
        QWidget* grid = createGridPage(it.value());
        tabWidget->addTab(grid, it.key());
    }
}

QWidget* MainWindow::createGridPage(const QVector<PurchaseItem>& items) {

    qDebug() << "Creating grid page for tab, item count:" << items.size();
    for (const auto& item : items) {
        qDebug() << "  presetId:" << item.presetId
            << " texture:" << item.texture;
    }
    const QString iconPath = levelEditRootPath + "/Always_Textures/PT Icons";
    const int kCols = 2;
    const int kHSpacing = 8;
    const int kVSpacing = 8;

    // Grid with forced cell sizes
    QWidget* gridHost = new QWidget;
    auto* grid = new QGridLayout(gridHost);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kHSpacing);
    grid->setVerticalSpacing(kVSpacing);
    grid->setAlignment(Qt::AlignTop);

    // place tiles
    int r = 0, c = 0;
    for (int i = 0; i < items.size(); ++i) {
        auto* t = new IconTileWidget(items[i], iconPath);
        t->setFixedSize(kTileW, kTileH);                       // hard size
        grid->addWidget(t, r, c, Qt::AlignHCenter | Qt::AlignTop);
        if (++c == kCols) { c = 0; ++r; }
        connect(t, &IconTileWidget::editRequested, this,
            [this, t](const PurchaseItem& current, IconTileWidget* self) {
                qDebug() << "Opening edit dialog for presetId:" << current.presetId
                    << "cost:" << current.cost
                    << "techLevel:" << current.techLevel
                    << "specialTechNumber:" << current.specialTechNumber
                    << "unitLimit:" << current.unitLimit
                    << "factory:" << current.factory
                    << "techBuilding:" << current.techBuilding
                    << "factoryNotRequired:" << current.factoryNotRequired;
                EditPurchaseItemDialog dlg(current, this);

                if (dlg.exec() == QDialog::Accepted) {
                    PurchaseItem updated = dlg.result();
                    self->applyEdits(updated);

                    // persist back into our model: find the matching item by presetId
                    // (or choose a stronger key if you prefer)
                    for (auto it = categorizedLists.begin(); it != categorizedLists.end(); ++it) {
                        for (PurchaseItem& pi : it.value()) {
                            if (pi.presetId == current.presetId) {
                                pi = updated;
                                return;
                            }
                        }
                    }
                }
            });
    }

    // pad last cell if odd
    if (c == 1) {
        auto* ph = new QWidget;
        ph->setFixedSize(kTileW, kTileH);
        ph->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        grid->addWidget(ph, r, 1, Qt::AlignHCenter | Qt::AlignTop);
    }

    // lock the strip width to exactly 2 columns
    const int stripW = kCols * kTileW + (kCols - 1) * kHSpacing;
    gridHost->setFixedWidth(stripW);
    gridHost->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    // center the strip
    QWidget* content = new QWidget;
    auto* center = new QHBoxLayout(content);
    center->setContentsMargins(6, 6, 6, 6);
    center->addStretch(1);
    center->addWidget(gridHost);
    center->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAlignment(Qt::AlignTop | Qt::AlignHCenter);   // belt & suspenders
    scroll->setWidget(content);
    return scroll;


}
void MainWindow::rebuildFromSelection() {
    // 1) collect only selected lists into categorizedLists
    categorizedLists.clear();
    for (const auto& pl : allLists) {
        if (!selectedListIds.contains(pl.id)) continue;
        const QString label = typeTeamToLabel(pl.type, pl.team);
        categorizedLists[label] += pl.items; // append then dedupe below
    }

    // 2) dedupe inside each tab (uses your canonical merge rules)
    for (auto it = categorizedLists.begin(); it != categorizedLists.end(); ++it) {
        dedupeWithinLabel(it.value());
    }

    // 3) rebuild UI tabs
    tabWidget->clear();
    buildTabs();
}
void MainWindow::showListPickerDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Choose Source Lists");
    dlg.resize(700, 500);

    auto* main = new QVBoxLayout(&dlg);

    // Helper: nice display text per list
    auto pretty = [&](const PurchaseList& pl) {
        const QString team = (pl.team == 0 ? "Allied" : "Soviet");
        QString typeStr;
        switch (pl.type) {
        case 0: typeStr = "Infantry"; break;
        case 1: typeStr = "Vehicles"; break;
        case 4: typeStr = "Extra Vehicles"; break;
        case 5: typeStr = "Air"; break;
        case 7: typeStr = "Extra Air"; break;
        case 6: typeStr = "Navy"; break;
        default: typeStr = QString("Type %1").arg(pl.type); break;
        }
        return QString("%1 / %2  %3").arg(team, typeStr, pl.name);
        };

    auto* list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto& pl : allLists) {
        auto* it = new QListWidgetItem(pretty(pl), list);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(selectedListIds.contains(pl.id) ? Qt::Checked : Qt::Unchecked);
        it->setData(Qt::UserRole, pl.id);
    }
    main->addWidget(list);

    auto* btnRow = new QHBoxLayout;
    auto* selectAll = new QPushButton("Select All");
    auto* clearAll = new QPushButton("Clear");
    auto* ok = new QPushButton("OK");
    auto* cancel = new QPushButton("Cancel");
    btnRow->addWidget(selectAll);
    btnRow->addWidget(clearAll);
    btnRow->addStretch();
    btnRow->addWidget(ok);
    btnRow->addWidget(cancel);
    main->addLayout(btnRow);

    connect(selectAll, &QPushButton::clicked, [=]() {
        for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Checked);
        });
    connect(clearAll, &QPushButton::clicked, [=]() {
        for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Unchecked);
        });
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(ok, &QPushButton::clicked, [&]() {
        QSet<QString> newSel;
        for (int i = 0; i < list->count(); ++i) {
            auto* it = list->item(i);
            if (it->checkState() == Qt::Checked)
                newSel.insert(it->data(Qt::UserRole).toString());
        }
        selectedListIds = std::move(newSel);
        QSettings s("SidebarTool", "SidebarEditor");
        s.setValue("SelectedLists", QStringList(selectedListIds.cbegin(), selectedListIds.cend()));
        dlg.accept();
        });

    if (dlg.exec() == QDialog::Accepted) {
        rebuildFromSelection();
    }
}

// --- CAMO reorder helpers (operate on parsed JSON items) ---------------------

static inline QString trimOrEmpty(const QString& s) { return s.trimmed(); }

// returns one of 'f','d','u','s', or 0 if no known suffix
static QChar camoCodeForTex(const QString& tex) {
    static QRegularExpression re("_(f|d|u|s)\\.dds$", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(tex);
    if (!m.hasMatch()) return QChar{};
    const QString c = m.captured(1).toLower();
    return c.isEmpty() ? QChar{} : c[0];
}

static QChar themeToCode(const QString& theme) {
    const QString t = theme.trimmed().toLower();
    if (t.startsWith('d')) return QChar('d'); // desert
    if (t.startsWith('u')) return QChar('u'); // urban
    if (t.startsWith('s')) return QChar('s'); // snow
    return QChar('f');                        // default forest
}

// Remove empties and duplicates, preserve first occurrence order
static QStringList dedupeKeepFirst(const QStringList& in) {
    QSet<QString> seen;
    QStringList out;
    out.reserve(in.size());
    for (const QString& s0 : in) {
        const QString s = trimOrEmpty(s0);
        if (s.isEmpty()) continue;
        if (seen.contains(s)) continue;
        seen.insert(s);
        out << s;
    }
    return out;
}

// Returns true if changed
static bool reorderItemTexturesForTheme(QJsonObject& item, QChar desired) {
    const QString base = trimOrEmpty(item.value(QStringLiteral("TEXTURE")).toString());
    QStringList pool;
    pool << base;

    const QJsonArray alts = item.value(QStringLiteral("ALT_TEXTURES")).toArray();
    for (const auto& v : alts) pool << v.toString();

    // de-dup + strip empties
    pool = dedupeKeepFirst(pool);
    if (pool.isEmpty()) return false;

    // Only bother if at least one entry has a camo suffix
    bool anyTagged = std::any_of(pool.cbegin(), pool.cend(), [](const QString& t) { return camoCodeForTex(t) != QChar{}; });
    if (!anyTagged) return false;

    // Stable sort: desired camo first, everything else after in original order
    // Use stable_sort on a vector of indices to preserve original relative order
    QVector<int> idx(pool.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        const bool aHit = (camoCodeForTex(pool[a]) == desired);
        const bool bHit = (camoCodeForTex(pool[b]) == desired);
        if (aHit != bHit) return aHit > bHit;
        return a < b; // stable by original index
        });

    QStringList sorted;
    for (int i : idx) sorted << pool[i];

    // Rebuild: first is TEXTURE, next up to 3 are ALT_TEXTURES
    const QString newBase = sorted.value(0);
    QStringList newAlts = sorted.mid(1, 3);
    while (newAlts.size() < 3) newAlts << "";

    bool changed = (newBase != base);
    // Compare ALT_TEXTURES content
    QJsonArray oldAltsJson = item.value(QStringLiteral("ALT_TEXTURES")).toArray();
    QStringList oldAlts;
    for (const auto& v : oldAltsJson) oldAlts << trimOrEmpty(v.toString());
    while (oldAlts.size() < 3) oldAlts << "";
    changed = changed || (oldAlts != newAlts);

    if (changed) {
        item.insert(QStringLiteral("TEXTURE"), newBase);
        QJsonArray newAltArr;
        for (const QString& s : newAlts) newAltArr.append(s);
        item.insert(QStringLiteral("ALT_TEXTURES"), newAltArr);
    }
    return changed;
}






// Batch camo reordering based on suffix priority
void MainWindow::applyCamoDefaults(const QString& mapTheme) {
    QMap<QString, int> priority = {
        {"forest", 0}, {"", 0}, {"_f", 0},
        {"desert", 1}, {"_d", 1},
        {"urban", 2}, {"_u", 2},
        {"snow", 3}, {"_s", 3}
    };

    QString suffix = mapTheme.toLower().left(1) == "d" ? "_d" :
        mapTheme.toLower().left(1) == "u" ? "_u" :
        mapTheme.toLower().left(1) == "s" ? "_s" : "_f";

    for (auto& list : categorizedLists) {
        for (PurchaseItem& item : list) {
            QStringList all = { item.texture };
            all.append(item.altTextures);
            all.erase(std::remove_if(all.begin(), all.end(), [](const QString& s) { return s.trimmed().isEmpty(); }), all.end());

            auto getPriority = [&](const QString& tex) -> int {
                QRegularExpression re("_(f|d|u|s)\\.dds$", QRegularExpression::CaseInsensitiveOption);
                auto match = re.match(tex);
                return match.hasMatch() ? priority.value("_" + match.captured(1), 0) : 0;
                };

            std::sort(all.begin(), all.end(), [&](const QString& a, const QString& b) {
                return getPriority(a) < getPriority(b);
                });

            item.texture = all.value(0);
            item.altTextures = all.mid(1, 3);
            while (item.altTextures.size() < 3)
                item.altTextures.append("");
        }
    }
}

// Export function
void MainWindow::exportMapJson() {
    QString path = QFileDialog::getSaveFileName(this, "Export JSON", "", "JSON Files (*.json)");
    if (path.isEmpty()) return;

    QJsonArray settingsArray;
    int factoryId = 263687;
    int baseId = 1000000000;
    int defCounter = 0;

    for (auto it = categorizedLists.begin(); it != categorizedLists.end(); ++it, ++defCounter) {
        QJsonArray itemsArray;
        for (const PurchaseItem& item : it.value()) {
            QJsonObject o;
            o["COST"] = item.cost;
            o["PRESET_ID"] = item.presetId;
            o["STRING_ID"] = item.stringId;
            o["TEXTURE"] = item.texture;
            o["TECH_LEVEL"] = item.techLevel;
            o["SPECIAL_TECH_NUMBER"] = item.specialTechNumber;
            o["UNIT_LIMIT"] = item.unitLimit;
            o["FACTORY"] = item.factory;
            o["TECH_BUILDING"] = item.techBuilding;
            o["FACTORY_NOT_REQUIRED"] = item.factoryNotRequired;

            QJsonArray altIds, altTex;
            for (int i = 0; i < 3; ++i) {
                altIds.append(item.altPresetIds.value(i, 0));
                altTex.append(item.altTextures.value(i, ""));
            }
            o["ALT_PRESETIDS"] = altIds;
            o["ALT_TEXTURES"] = altTex;
            itemsArray.append(o);
        }

        QJsonObject defBlock = {
            {"FACTORY_ID", factoryId},
            {"FACTORY_WRAPPER", QJsonObject{
                {"DATA", QJsonObject{
                    {"DEFINITION_BASE", QJsonObject{
                        {"ID", baseId + defCounter},
                        {"NAME", QString::number(defCounter + 1)}
                    }},
                    {"PURCHASE_SETTINGS_DEF_CLASS", QJsonObject{
                        {"TEAM", it.value().value(0).team},
                        {"TYPE", it.value().value(0).type},
                        {"PURCHASE_ITEMS", itemsArray}
                    }}
                }}
            }}
        };

        settingsArray.append(defBlock);
    }

    QJsonObject root;
    root["GlobalSettings"] = settingsArray;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
    }
}
void MainWindow::applyEditsToPurchaseList(PurchaseList& pl,
    const QHash<QString, PurchaseItem>& edits)
{
    for (PurchaseItem& it : pl.items) {
        const QString key = canonicalPresetKey(it);
        auto found = edits.find(key);
        if (found == edits.end()) continue;

        const PurchaseItem& src = found.value();
        // Overwrite everything that belongs to the item (retain team/type by list)
        it.cost = src.cost;
        it.texture = src.texture;
        it.techLevel = src.techLevel;
        it.specialTechNumber = src.specialTechNumber;
        it.unitLimit = src.unitLimit;
        it.factory = src.factory;
        it.techBuilding = src.techBuilding;
        it.factoryNotRequired = src.factoryNotRequired;

        it.altPresetIds = src.altPresetIds;
        it.altTextures = src.altTextures;
        // keep it.presetId as is (it should match anyway)
    }
}

void MainWindow::updateMasterFromTabs() {
    const auto edits = buildEditedMapFromTabs();
    PatchOptions opt;
    const QString masterPath = levelEditRootPath + "/Database/Global/Definitions/GlobalSettings.json";
    QFile f(masterPath);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Load failed", masterPath);
        return;
    }
    QString json = QString::fromUtf8(f.readAll());
    f.close();

    int patched = 0;
    // Only lists the user selected (id = TEAM/TYPE/NAME)
    for (const PurchaseList& pl : allLists) {
        if (!selectedListIds.contains(pl.id)) continue;

        for (const PurchaseItem& it : pl.items) {
            // Use your canonical grouping to fetch the edited value
            const QString key = [&] {
                QVector<int> ids; ids << it.presetId;
                for (int id : it.altPresetIds) if (id) ids << id;
                std::sort(ids.begin(), ids.end());
                ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
                QString k; for (int id : ids) { k += QString::number(id); k += '_'; }
                return k;
                }();
            auto e = edits.constFind(key);
            if (e == edits.constEnd()) continue;

            if (patchPurchaseItemInText(json, pl.team, pl.type, it.presetId, *e, pl.name, opt, nullptr))
                ++patched;
        }
    }

    if (!patched) {
        QMessageBox::information(this, "No changes", "Nothing to update.");
        return;
    }

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Write failed", masterPath);
        return;
    }
    f.write(json.toUtf8());
    f.close();

    QMessageBox::information(this, "Master updated",
        QString("Patched %1 item%2.").arg(patched).arg(patched == 1 ? "" : "s"));
}




void MainWindow::updateMasterFromTabsAllLists() {
    const auto edits = buildEditedMapFromTabs();
    PatchOptions opt;
    const QString masterPath = levelEditRootPath + "/Database/Global/Definitions/GlobalSettings.json";

    QFile f(masterPath);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Load failed", masterPath);
        return;
    }
    QString json = QString::fromUtf8(f.readAll());
    f.close();

    int patched = 0;

    for (const PurchaseList& pl : allLists) {
        for (const PurchaseItem& it : pl.items) {
            // canonical key for the edited group
            const QString key = [&] {
                QVector<int> ids; ids << it.presetId;
                for (int id : it.altPresetIds) if (id) ids << id;
                std::sort(ids.begin(), ids.end());
                ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
                QString k; for (int id : ids) { k += QString::number(id) + '_'; }
                return k;
                }();

            auto e = edits.constFind(key);
            if (e == edits.constEnd()) continue;

            // Scan the entire file for all occurrences of TEAM/TYPE + PRESET_ID
            int pos = 0;
            while (true) {
                bool found = false;
                const bool changed = patchPurchaseItemInText(
                    json,
                    pl.team, pl.type, it.presetId,
                    *e,
                    /*listNameOptional*/ QString(), // IMPORTANT: no name filter  hits all camos
                    opt,
                    &found,
                    &pos
                );
                if (changed) ++patched;
                if (!found) break; // no more matches in the rest of the file
            }
        }
    }

    if (!patched) {
        QMessageBox::information(this, "No changes", "Nothing to update.");
        return;
    }

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Write failed", masterPath);
        return;
    }
    f.write(json.toUtf8());
    f.close();

    QMessageBox::information(this, "Master updated",
        QString("Patched %1 item%2.").arg(patched).arg(patched == 1 ? "" : "s"));
}

/*
QJsonObject* MainWindow::findOrCreateSection(QJsonArray& globalSettings,
    int team, int type, int& nextDefId) const
{
    // Look for existing section with matching TEAM/TYPE
    for (auto it = globalSettings.begin(); it != globalSettings.end(); ++it) {
        QJsonObject obj = it->toObject();
        QJsonObject ps = obj["FACTORY_WRAPPER"].toObject()
            ["DATA"].toObject()
            ["PURCHASE_SETTINGS_DEF_CLASS"].toObject();
        if (ps["TEAM"].toInt() == team && ps["TYPE"].toInt() == type) {
            // return a pointer to the real object inside the array
            return &(*it).toObject(); // NB: can't return address of temp; see below
        }
    }
    // If not found, create a new block
    QJsonArray items; // empty
    QJsonObject defBlock = {
        {"FACTORY_ID", 263687},
        {"FACTORY_WRAPPER", QJsonObject{
            {"DATA", QJsonObject{
                {"DEFINITION_BASE", QJsonObject{
                    {"ID", nextDefId++},
                    {"NAME", QString("%1").arg(nextDefId - 1)} // simple numbered name
                }},
                {"PURCHASE_SETTINGS_DEF_CLASS", QJsonObject{
                    {"TEAM", team},
                    {"TYPE", type},
                    {"PURCHASE_ITEMS", items}
                }}
            }}
        }}
    };
    globalSettings.append(defBlock);
    // return pointer to the just-appended object
    return &globalSettings[globalSettings.size() - 1].toObject(); // (see note below)
}
*/
/*
void MainWindow::mergeIntoLevelDoc(QJsonDocument& levelDoc,
    const QMap<QString, QVector<PurchaseItem>>& tabs)
{
    QJsonObject root = levelDoc.isNull() ? QJsonObject{} : levelDoc.object();
    if (root.isEmpty()) {
        root["GlobalSettings"] = QJsonArray{};
    }
    QJsonArray gs = root["GlobalSettings"].toArray();

    // Find the next definition ID (keep stable if file already has them)
    int nextDefId = 1000000000;
    for (const auto& v : gs) {
        const QJsonObject db = v.toObject()
            ["FACTORY_WRAPPER"].toObject()
            ["DATA"].toObject()
            ["DEFINITION_BASE"].toObject();
        nextDefId = std::max(nextDefId, db["ID"].toInt() + 1);
    }

    // Build a 2-key map TEAM/TYPE -> edited items we want to merge
    QMap<QPair<int, int>, QVector<PurchaseItem>> perSection;
    for (auto it = tabs.cbegin(); it != tabs.cend(); ++it) {
        for (const PurchaseItem& item : it.value()) {
            perSection[{item.team, item.type}].append(item);
        }
    }

    // Merge each section
    for (auto sec = perSection.cbegin(); sec != perSection.cend(); ++sec) {
        const int team = sec.key().first;
        const int type = sec.key().second;

        // Find or create section index
        int idx = -1;
        for (int i = 0; i < gs.size(); ++i) {
            const QJsonObject ps = gs[i].toObject()
                ["FACTORY_WRAPPER"].toObject()
                ["DATA"].toObject()
                ["PURCHASE_SETTINGS_DEF_CLASS"].toObject();
            if (ps["TEAM"].toInt() == team && ps["TYPE"].toInt() == type) { idx = i; break; }
        }
        if (idx < 0) {
            // create an empty section
            QJsonObject defBlock = {
                {"FACTORY_ID", 263687},
                {"FACTORY_WRAPPER", QJsonObject{
                    {"DATA", QJsonObject{
                        {"DEFINITION_BASE", QJsonObject{
                            {"ID", nextDefId++},
                            {"NAME", QString("%1").arg(nextDefId - 1)}
                        }},
                        {"PURCHASE_SETTINGS_DEF_CLASS", QJsonObject{
                            {"TEAM", team},
                            {"TYPE", type},
                            {"PURCHASE_ITEMS", QJsonArray{}}
                        }}
                    }}
                }}
            };
            gs.append(defBlock);
            idx = gs.size() - 1;
        }

        // Pull, modify, and write back the section object
        QJsonObject block = gs[idx].toObject();
        QJsonObject data = block["FACTORY_WRAPPER"].toObject()["DATA"].toObject();
        QJsonObject ps = data["PURCHASE_SETTINGS_DEF_CLASS"].toObject();
        QJsonArray  itemsJson = ps["PURCHASE_ITEMS"].toArray();

        // Build index by PRESET_ID for quick update
        QHash<int, int> indexByPreset;
        for (int i = 0; i < itemsJson.size(); ++i) {
            indexByPreset[itemsJson[i].toObject()["PRESET_ID"].toInt()] = i;
        }

        // Merge: update existing by PRESET_ID; otherwise append
        for (const PurchaseItem& it : sec.value()) {
            if (indexByPreset.contains(it.presetId)) {
                itemsJson[indexByPreset[it.presetId]] = itemToJson(it);
            }
            else {
                itemsJson.append(itemToJson(it));
            }
        }

        // write back
        ps["PURCHASE_ITEMS"] = itemsJson;
        data["PURCHASE_SETTINGS_DEF_CLASS"] = ps;
        block["FACTORY_WRAPPER"].toObject()["DATA"] = data; // cant assign like that; do it in steps:
        QJsonObject fw = block["FACTORY_WRAPPER"].toObject();
        fw["DATA"] = data;
        block["FACTORY_WRAPPER"] = fw;
        gs[idx] = block;
    }

    root["GlobalSettings"] = gs;
    levelDoc = QJsonDocument(root);
}
*/
void MainWindow::showLevelPickerAndRun(std::function<void(const QStringList&)> fn) {
    // Collect RA_* dirs
    QString basePath = levelEditRootPath + "/Database/Levels";
    QDir baseDir(basePath);
    const QStringList maps = baseDir.entryList(QStringList("RA_*"), QDir::Dirs | QDir::NoDotAndDotDot);

    QDialog dlg(this);
    dlg.setWindowTitle("Select Levels to Update");
    dlg.resize(500, 500);
    auto* v = new QVBoxLayout(&dlg);

    auto* list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const QString& m : maps) list->addItem(m);
    v->addWidget(list);

    auto* row = new QHBoxLayout;
    auto* all = new QPushButton("Select All");
    auto* none = new QPushButton("Clear");
    auto* ok = new QPushButton("Update");
    auto* cancel = new QPushButton("Cancel");
    row->addWidget(all); row->addWidget(none);
    row->addStretch(); row->addWidget(ok); row->addWidget(cancel);
    v->addLayout(row);

    connect(all, &QPushButton::clicked, [=]() {
        list->selectAll();
        });
    connect(none, &QPushButton::clicked, [=]() {
        list->clearSelection();
        });
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(ok, &QPushButton::clicked, [&]() {
        QStringList chosen;
        for (QListWidgetItem* it : list->selectedItems()) chosen << it->text();
        if (!chosen.isEmpty()) fn(chosen);
        dlg.accept();
        });

    dlg.exec();
}
int MainWindow::computeNextUniqueDefId(const QJsonDocument* currentLevelDoc) const
{
    int maxId = 0;

    // Parse either shape:
    //  A) Definitions: GlobalSettings[*].FACTORY_WRAPPER.DATA.DEFINITION_BASE.ID
    //  B) Presets:     GlobalSettings[*].DEF_ID
    auto scanDoc = [&](const QJsonDocument& doc) {
        if (!doc.isObject()) return;
        const QJsonObject root = doc.object();
        const QJsonArray gs = root.value(QStringLiteral("GlobalSettings")).toArray();
        for (const QJsonValue& v : gs) {
            const QJsonObject o = v.toObject();

            // Try Definitions shape first
            const QJsonObject def =
                o.value(QStringLiteral("FACTORY_WRAPPER")).toObject()
                .value(QStringLiteral("DATA")).toObject()
                .value(QStringLiteral("DEFINITION_BASE")).toObject();

            if (!def.isEmpty()) {
                maxId = std::max(maxId, def.value(QStringLiteral("ID")).toInt());
                continue;
            }

            // Fall back to Presets shape
            const int defId = o.value(QStringLiteral("DEF_ID")).toInt(0);
            if (defId > 0) maxId = std::max(maxId, defId);
        }
        };

    // Walk a tree of *.json and scan each doc
    auto scanTree = [&](const QString& rootDir) {
        QDirIterator it(rootDir, QStringList() << "*.json",
            QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            QJsonDocument d;
            if (loadJsonFromFile(p, d)) scanDoc(d);
        }
        };

    // 1) All JSON files under both Global/Definitions and Global/Presets (recursive)
    const QString defsRoot = levelEditRootPath + "/Database/Global/Definitions";
    const QString presRoot = levelEditRootPath + "/Database/Global/Presets";
    scanTree(defsRoot);
    scanTree(presRoot);

    // 2) Also consider the current level (if provided)
    if (currentLevelDoc && !currentLevelDoc->isNull()) {
        scanDoc(*currentLevelDoc);
    }

    // Keep IDs >= 1,000,000,001 and monotonic
    const int floorId = 1000000001;
    return std::max(maxId + 1, floorId);
}

// Scan ALL existing DEF_IDs so we avoid duplicates across this run.
static QSet<int> collectAllExistingDefIds(const QString& root) {
    QSet<int> ids;

    auto scanFile = [&](const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonParseError pe{};
        const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
        f.close();
        if (pe.error != QJsonParseError::NoError || !d.isObject()) return;

        const auto gs = d.object().value(QStringLiteral("GlobalSettings")).toArray();
        for (const auto& v : gs) {
            const QJsonObject db = v.toObject()
                .value(QStringLiteral("FACTORY_WRAPPER")).toObject()
                .value(QStringLiteral("DATA")).toObject()
                .value(QStringLiteral("DEFINITION_BASE")).toObject();
            const int id = db.value(QStringLiteral("ID")).toInt();
            if (id) ids.insert(id);
        }
        };

    // Global/Definitions/**
    {
        QDirIterator it(root + "/Database/Global/Definitions",
            QStringList{ "*.json" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) scanFile(it.next());
    }
    // Global/Presets/**
    {
        QDirIterator it(root + "/Database/Global/Presets",
            QStringList{ "*.json" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) scanFile(it.next());
    }
    // Levels/*/Definitions/GlobalSettings.json
    {
        QDirIterator it(root + "/Database/Levels",
            QStringList{ "GlobalSettings.json" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (!p.contains("/Definitions/GlobalSettings.json")) continue;
            scanFile(p);
        }
    }
    return ids;
}

// Simple allocator that hands out never-before-seen IDs in this run.
struct DefIdAllocator {
    QSet<int> used;
    int next;

    explicit DefIdAllocator(const QSet<int>& already) : used(already) {
        int maxId = 1000000000;
        for (int id : used) maxId = std::max(maxId, id);
        next = std::max(1000000000, maxId + 1);
        while (used.contains(next)) ++next;
    }
    int take() {
        int id = next;
        used.insert(id);
        do { ++next; } while (used.contains(next));
        return id;
    }
};


void MainWindow::updateSelectedLevels() {
    const auto tabs = categorizedLists;

    showLevelPickerAndRun([&](const QStringList& chosen) {
        QStringList ok, fail;

        // Build per-run allocator (scan disk once so we never collide)
        DefIdAllocator idAlloc(collectAllExistingDefIds(levelEditRootPath));

        struct CreatedRow { QString level; int team; int type; int id; QString name; };
        QVector<CreatedRow> createdRows;

        const QString masterPath = levelEditRootPath + "/Database/Global/Definitions/GlobalSettings.json";
        static const auto rawParentMap = buildParentRefMapFromMaster(
            masterPath,
            [](const QString& nm) { return !nm.contains("(Neutral)", Qt::CaseInsensitive); }
        );
        static const QHash<TeamType, int> parentByTT_Fallback = flattenParentMap(rawParentMap);

        auto parentByTTForLevel = [&](const QString& levelName) -> QHash<TeamType, int> {
            const QString theme = normalizeTheme(mapCamoAssignments.value(levelName));
            QHash<TeamType, int> out;

            // 1) seed with first-seen choice for each (TEAM,TYPE) based on selected lists
            for (const QString& listId : selectedListIds) {
                int team = 0, type = 0; QString srcName;
                if (!parseListId(listId, team, type, &srcName)) continue;
                TeamType key{ team, type };
                if (!out.contains(key)) {
                    out.insert(key, gParentIdByListId.value(listId, 0));
                }
            }
            // 2) if we have a themed list in the selection, prefer that as the parent
            for (const QString& listId : selectedListIds) {
                int team = 0, type = 0; QString srcName;
                if (!parseListId(listId, team, type, &srcName)) continue;
                if (!gNameByListId.contains(listId)) continue;
                const QString defName = gNameByListId.value(listId).toLower();
                if (!theme.isEmpty() && defName.contains(theme)) {
                    out[TeamType{ team, type }] = gParentIdByListId.value(listId, 0);
                }
            }
            // 3) still missing anything? fall back to master map
            for (auto it = parentByTT_Fallback.cbegin(); it != parentByTT_Fallback.cend(); ++it) {
                if (!out.contains(it.key())) out.insert(it.key(), it.value());
            }
            return out;
            };

        for (const QString& level : chosen) {
            const QHash<TeamType, int> parentByTT = parentByTTForLevel(level);

            const QString path = QString("%1/Database/Levels/%2/Definitions/GlobalSettings.json")
                .arg(levelEditRootPath, level);

            QString json;
            {
                QFile f(path);
                if (f.exists()) {
                    if (!f.open(QIODevice::ReadOnly)) { fail << level; continue; }
                    json = QString::fromUtf8(f.readAll());
                    f.close();
                }
                else {
                    // new file shell
                    json = "{\n\t\"SCHEMA_VERSION\": 1,\n\t\"GlobalSettings\": [\n\t]\n}\n";
                    QDir().mkpath(QFileInfo(path).path());
                }
            }

            int patched = 0, appended = 0, createdSections = 0;
            PatchOptions opt; // coreFields=true, textures=false, altArrays=false

            // Group visible items by (TEAM,TYPE)
            QMap<QPair<int, int>, QVector<const PurchaseItem*>> perSection;
            for (auto it = tabs.cbegin(); it != tabs.cend(); ++it)
                for (const PurchaseItem& pi : it.value())
                    perSection[{pi.team, pi.type}].append(&pi);

            // Ensure section exists, then patch/append items
            for (auto sec = perSection.cbegin(); sec != perSection.cend(); ++sec) {
                const int team = sec.key().first;
                const int type = sec.key().second;

                // Does a block with this TEAM/TYPE exist?
                QRegularExpression secTeam(QStringLiteral("\"TEAM\"\\s*:\\s*%1").arg(team));
                auto teamMatch = secTeam.match(json);
                bool hasSection = false;
                if (teamMatch.hasMatch()) {
                    QRegularExpression secType(QStringLiteral("\"TYPE\"\\s*:\\s*%1").arg(type));
                    hasSection = secType.match(json, teamMatch.capturedStart()).hasMatch();
                }

                // Create section if missing
                if (!hasSection) {
                    const QString i0 = arrayElemIndent(json, QStringLiteral("GlobalSettings"));
                    const QString friendlyName =
                        QStringLiteral("Sidebar Editor Custom %1 %2 List")
                        .arg(teamWord(team), typeWord(type));

                    const int newId = idAlloc.take();
                    const QString block = buildSectionBlockText(newId, friendlyName, team, type, i0);
                    if (!appendSectionBlock(json, block)) { fail << level; goto after_level; }

                    createdRows.push_back({ level, team, type, newId, friendlyName });
                    ++createdSections;
                }

                // Patch/append items
                for (const PurchaseItem* ppi : sec.value()) {
                    const PurchaseItem& pi = *ppi;
                    bool existed = false;
                    if (patchPurchaseItemInText(json, team, type, pi.presetId, pi,
                        /*listNameOptional*/ QString(), opt, &existed)) {
                        ++patched;
                        continue;
                    }
                    if (!existed) {
                        if (appendItemToSection(json, team, type, pi)) {
                            ++appended;
                        }
                    }
                }
            }

            // Write back the Definitions file once per level
            {
                QFile wf(path);
                if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) { fail << level; continue; }
                wf.write(json.toUtf8());
                wf.close();
            }

            // Also emit the Presets/GlobalSettings.json (schema/version at top)
            if (!writeLevelPresetsGlobalSettings(levelEditRootPath, level, json, parentByTT)) {
                qWarning() << "Failed to generate Presets/GlobalSettings.json for" << level;
            }

            ok << QString("%1 (patched %2, appended %3%4)")
                .arg(level).arg(patched).arg(appended)
                .arg(createdSections ? QString(", new sections %1").arg(createdSections) : QString());
            continue;

        after_level:
            // hard error inside the level loop
            fail << level;
        }

        if (!createdRows.isEmpty()) {
            qDebug() << "===== Newly assigned DEF_IDs in this run =====";
            for (const auto& r : createdRows) {
                qDebug().noquote() << QString("%1  DEF_ID=%2  NAME=\"%3\"  TEAM=%4 TYPE=%5")
                    .arg(r.level)
                    .arg(r.id)
                    .arg(r.name)
                    .arg(r.team)
                    .arg(r.type);
            }
        }

        QMessageBox msg;
        msg.setWindowTitle("Level update");
        msg.setIcon(QMessageBox::Information);
        msg.setText(QString("Updated %1 levels\nFailures: %2")
            .arg(ok.size())
            .arg(fail.isEmpty() ? "None" : fail.join(", ")));
        msg.exec();
        });
}



/**/
void MainWindow::exportAllMapJsons() {
    QStringList successList, failList;

    for (auto it = mapCamoAssignments.begin(); it != mapCamoAssignments.end(); ++it) {
        QString mapName = it.key();
        QString theme = it.value();
        applyCamoDefaults(theme);

        QJsonArray settingsArray;
        int factoryId = 263687;
        int baseId = 1000000000;
        int defCounter = 0;

        for (auto ctg = categorizedLists.begin(); ctg != categorizedLists.end(); ++ctg, ++defCounter) {
            QJsonArray itemsArray;
            for (const PurchaseItem& item : ctg.value()) {
                QJsonObject o;
                o["COST"] = item.cost;
                o["PRESET_ID"] = item.presetId;
                o["STRING_ID"] = item.stringId;
                o["TEXTURE"] = item.texture;
                o["TECH_LEVEL"] = item.techLevel;
                o["SPECIAL_TECH_NUMBER"] = item.specialTechNumber;
                o["UNIT_LIMIT"] = item.unitLimit;
                o["FACTORY"] = item.factory;
                o["TECH_BUILDING"] = item.techBuilding;
                o["FACTORY_NOT_REQUIRED"] = item.factoryNotRequired;

                QJsonArray altIds, altTex;
                for (int i = 0; i < 3; ++i) {
                    altIds.append(item.altPresetIds.value(i, 0));
                    altTex.append(item.altTextures.value(i, ""));
                }
                o["ALT_PRESETIDS"] = altIds;
                o["ALT_TEXTURES"] = altTex;
                itemsArray.append(o);
            }

            QJsonObject defBlock = {
                {"FACTORY_ID", factoryId},
                {"FACTORY_WRAPPER", QJsonObject{
                    {"DATA", QJsonObject{
                        {"DEFINITION_BASE", QJsonObject{
                            {"ID", baseId + defCounter},
                            {"NAME", QString::number(defCounter + 1)}
                        }},
                        {"PURCHASE_SETTINGS_DEF_CLASS", QJsonObject{
                            {"TEAM", ctg.value().value(0).team},
                            {"TYPE", ctg.value().value(0).type},
                            {"PURCHASE_ITEMS", itemsArray}
                        }}
                    }}
                }}
            };

            settingsArray.append(defBlock);
        }

        QJsonObject root;
        root["GlobalSettings"] = settingsArray;

        QString outPath = QString("%1/Database/Levels/%2/Definitions/GlobalSettings.json").arg(levelEditRootPath, mapName);
        QDir().mkpath(QFileInfo(outPath).path());
        QFile outFile(outPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            outFile.close();
            successList.append(mapName);
        }
        else {
            failList.append(mapName);
        }
    }

    QMessageBox msg;
    msg.setWindowTitle("Export Report");
    msg.setIcon(QMessageBox::Information);
    msg.setText(QString("Exported %1 maps\nFailures: %2")
        .arg(successList.size())
        .arg(failList.isEmpty() ? "None" : failList.join(", ")));
    msg.exec();
}
// Insert a new object block before the closing ']' of GlobalSettings:[...]
// Insert a new object block before the closing ']' of GlobalSettings:[...]
static bool appendSectionBlock(QString& json, const QString& blockJson) {
    // Find "GlobalSettings": [
    QRegularExpression re(QStringLiteral("\"GlobalSettings\"\\s*:\\s*\\["));
    auto m = re.match(json);
    if (!m.hasMatch()) return false;

    const int arrOpen = m.capturedEnd(); // right after '['

    // Find matching ']'
    int i = arrOpen, depth = 1;
    while (i < json.size() && depth > 0) {
        const QChar ch = json.at(i++);
        if (ch == '[') ++depth;
        else if (ch == ']') --depth;
        else if (ch == '\"') {
            while (i < json.size()) {
                if (json.at(i) == '\\') { i += 2; continue; }
                if (json.at(i) == '\"') { ++i; break; }
                ++i;
            }
        }
    }
    if (depth != 0) return false;
    const int arrEnd = i - 1; // index of the closing ']'

    // Indentation of the line that contains ']'
    int lineStart = json.lastIndexOf('\n', arrEnd);
    QString endIndent;
    if (lineStart >= 0) {
        int j = lineStart + 1;
        while (j < json.size() && (json.at(j) == ' ' || json.at(j) == '\t')) {
            endIndent += json.at(j); ++j;
        }
    }

    // Is the array empty (ignoring whitespace)?
    const QString between = json.mid(arrOpen, arrEnd - arrOpen);
    const bool isEmpty = between.trimmed().isEmpty();

    if (isEmpty) {
        // Replace *all* whitespace between '[' and ']' so there is no blank line.
        const QString replacement = QStringLiteral("\n") + blockJson + QStringLiteral("\n") + endIndent;
        json.replace(arrOpen, arrEnd - arrOpen, replacement);
    }
    else {
        // Find start of trailing whitespace before ']'
        int tailPos = arrEnd - 1;
        while (tailPos >= arrOpen && json.at(tailPos).isSpace()) --tailPos;
        if (tailPos < arrOpen) return false;
        const int wsStart = tailPos + 1;

        // Do NOT keep the existing indent here (it caused the extra tab).
        // Emit: ",\n" + block + "\n" + indent_of_']'
        const QString replacement = QStringLiteral(",\n") + blockJson + QStringLiteral("\n") + endIndent;
        json.replace(wsStart, arrEnd - wsStart, replacement);
    }
    return true;
}

// Reorder camo textures in an existing level's Definitions/GlobalSettings.json
// using the theme assigned to that level. Returns true if the file was changed.
bool MainWindow::reorderCamoInLevelFile(const QString& level, const QString& theme) {
    const QString path = QString("%1/Database/Levels/%2/Definitions/GlobalSettings.json")
        .arg(levelEditRootPath, level);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "reorderCamoInLevelFile: open failed" << path;
        return false;
    }
    QString json = QString::fromUtf8(f.readAll());
    f.close();

    const QChar desired = themeToCode(theme);
    if (desired.isNull()) return false;

    auto camoCodeForTex = [](const QString& tex) -> QChar {
        static QRegularExpression re("_(f|d|u|s)\\.dds$", QRegularExpression::CaseInsensitiveOption);
        auto m = re.match(tex);
        if (!m.hasMatch()) return QChar{};
        const QString c = m.captured(1).toLower();
        return c.isEmpty() ? QChar{} : c[0];
        };

    bool anyChanged = false;
    int searchPos = 0;

    while (true) {
        int keyPos = json.indexOf(QStringLiteral("\"PURCHASE_ITEMS\""), searchPos);
        if (keyPos < 0) break;

        int arrOpen = json.indexOf('[', keyPos);
        if (arrOpen < 0) break;

        int i = arrOpen + 1, depth = 1;
        while (i < json.size() && depth > 0) {
            const QChar ch = json.at(i++);
            if (ch == '\"') {
                while (i < json.size()) {
                    if (json.at(i) == '\\') { i += 2; continue; }
                    if (json.at(i) == '\"') { ++i; break; }
                    ++i;
                }
            }
            else if (ch == '[') ++depth;
            else if (ch == ']') --depth;
        }
        if (depth != 0) break;
        const int arrClose = i - 1;

        QString arrBody = json.mid(arrOpen + 1, arrClose - (arrOpen + 1));
        int arrLocalPos = 0;
        bool changedThisArray = false;

        while (true) {
            int objOpen = arrBody.indexOf('{', arrLocalPos);
            if (objOpen < 0) break;

            int j = objOpen + 1, objDepth = 1;
            while (j < arrBody.size() && objDepth > 0) {
                const QChar ch2 = arrBody.at(j++);
                if (ch2 == '\"') {
                    while (j < arrBody.size()) {
                        if (arrBody.at(j) == '\\') { j += 2; continue; }
                        if (arrBody.at(j) == '\"') { ++j; break; }
                        ++j;
                    }
                }
                else if (ch2 == '{') ++objDepth;
                else if (ch2 == '}') --objDepth;
            }
            if (objDepth != 0) break;

            const int objClose = j;
            QString obj = arrBody.mid(objOpen, objClose - objOpen);

            // Read current values (parse only to read)
            QJsonParseError pe{};
            QJsonDocument d = QJsonDocument::fromJson(obj.toUtf8(), &pe);
            if (pe.error == QJsonParseError::NoError && d.isObject()) {
                QJsonObject o = d.object();

                const int baseId = o.value(QStringLiteral("PRESET_ID")).toInt();
                const QString baseTex = o.value(QStringLiteral("TEXTURE")).toString().trimmed();

                QJsonArray altIdsA = o.value(QStringLiteral("ALT_PRESETIDS")).toArray();
                QJsonArray altTexA = o.value(QStringLiteral("ALT_TEXTURES")).toArray();

                struct Pair { int id; QString tex; bool tagged; bool hasTex; };
                QVector<Pair> pairs;
                pairs.reserve(4);

                auto addPair = [&](int id, const QString& tex) {
                    const QString t = tex.trimmed();
                    const bool tagged = (!t.isEmpty() && camoCodeForTex(t) == desired);
                    pairs.push_back(Pair{ id, t, tagged, !t.isEmpty() });
                    };

                addPair(baseId, baseTex);
                for (int k = 0; k < 3; ++k) {
                    const int id = (k < altIdsA.size() ? altIdsA.at(k).toInt() : 0);
                    const QString tx = (k < altTexA.size() ? altTexA.at(k).toString() : QString());
                    addPair(id, tx);
                }

                // See if at least one entry has a camo tag; otherwise skip
                const bool anyTagged = std::any_of(pairs.cbegin(), pairs.cend(),
                    [](const Pair& p) { return p.tagged; });

                if (anyTagged) {
                    // Stable priority: desired camo first; keep original relative order otherwise
                    QVector<int> idx(pairs.size());
                    std::iota(idx.begin(), idx.end(), 0);
                    std::stable_sort(idx.begin(), idx.end(),
                        [&](int a, int b) {
                            if (pairs[a].tagged != pairs[b].tagged) return pairs[a].tagged; // true first
                            return a < b;
                        });

                    // Rebuild base + 3 alts from ordered pairs
                    int newBaseId = baseId;
                    QString newBaseTex = baseTex;
                    QVector<int> newAltIds; newAltIds.reserve(3);
                    QVector<QString> newAltTex; newAltTex.reserve(3);

                    if (!idx.isEmpty()) {
                        newBaseId = pairs[idx[0]].id;
                        newBaseTex = pairs[idx[0]].tex;
                    }
                    for (int n = 1; n <= 3; ++n) {
                        if (n < idx.size()) {
                            newAltIds << pairs[idx[n]].id;
                            newAltTex << pairs[idx[n]].tex;
                        }
                        else {
                            newAltIds << 0;
                            newAltTex << "";
                        }
                    }

                    // Only write if something actually changed
                    bool changedThisObj = false;
                    if (newBaseTex != baseTex) {
                        replaceKeyLiteral(obj, QStringLiteral("TEXTURE"), jsonQuote(newBaseTex));
                        changedThisObj = true;
                    }
                    if (newBaseId != baseId) {
                        replaceKeyLiteral(obj, QStringLiteral("PRESET_ID"), QString::number(newBaseId));
                        changedThisObj = true;
                    }

                    // Normalize old arrays to length 3 for compare
                    QVector<int> oldAltIds(3, 0);
                    QVector<QString> oldAltTex(3, "");
                    for (int k = 0; k < 3; ++k) {
                        if (k < altIdsA.size()) oldAltIds[k] = altIdsA.at(k).toInt();
                        if (k < altTexA.size()) oldAltTex[k] = altTexA.at(k).toString().trimmed();
                    }

                    if (oldAltIds != newAltIds) {
                        replaceArrayIntsPreserving(obj, QStringLiteral("ALT_PRESETIDS"), newAltIds);
                        changedThisObj = true;
                    }
                    if (oldAltTex != newAltTex) {
                        replaceArrayStringsPreserving(obj, QStringLiteral("ALT_TEXTURES"), newAltTex);
                        changedThisObj = true;
                    }

                    if (changedThisObj) {
                        arrBody.replace(objOpen, objClose - objOpen, obj);
                        changedThisArray = true;
                        anyChanged = true;
                    }
                }
            }

            arrLocalPos = objOpen + obj.size(); // move past (possibly) replaced object
        }

        if (changedThisArray) {
            json.replace(arrOpen + 1, arrClose - (arrOpen + 1), arrBody);
        }
        searchPos = arrClose + 1;
    }

    if (!anyChanged) return false;

    QFile wf(path);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "reorderCamoInLevelFile: write failed" << path;
        return false;
    }
    wf.write(json.toUtf8());
    wf.close();
    return true;
}


// Build a section block (empty PURCHASE_ITEMS) with consistent indentation
static QString buildSectionBlockText(int defId, const QString& name, int team, int type,
    const QString& i0) {
    const QString i1 = i0 + "\t";
    const QString i2 = i1 + "\t";
    const QString i3 = i2 + "\t";

    return
        i0 + "{\n" +
        i1 + "\"FACTORY_ID\": 263687,\n" +
        i1 + "\"FACTORY_WRAPPER\": {\n" +
        i2 + "\"DATA\": {\n" +
        i3 + "\"DEFINITION_BASE\": {\n" +
        i3 + "\t\"ID\": " + QString::number(defId) + ",\n" +
        i3 + "\t\"NAME\": " + jsonQuote(name.isEmpty() ? QString::number(defId) : name) + "\n" +
        i3 + "},\n" +
        i3 + "\"PURCHASE_SETTINGS_DEF_CLASS\": {\n" +
        i3 + "\t\"TEAM\": " + QString::number(team) + ",\n" +
        i3 + "\t\"TYPE\": " + QString::number(type) + ",\n" +
        i3 + "\t\"PURCHASE_ITEMS\": []\n" +
        i3 + "}\n" +
        i2 + "}\n" +
        i1 + "}\n" +
        i0 + "}";
}



// Append a purchase item object to the section's PURCHASE_ITEMS array.
// If array has elements, we add ",\n"; if empty we just insert the item.
static bool appendItemToSection(QString& json, int team, int type, const PurchaseItem& it,
    const QString& listNameOptional)
{
    // 1) Narrow to the correct PURCHASE_SETTINGS_DEF_CLASS (TEAM+TYPE in SAME object)
    int pos = 0;
    int arrStart = -1, arrEnd = -1;

    while (true) {
        const int anchor = json.indexOf(QStringLiteral("\"PURCHASE_SETTINGS_DEF_CLASS\""), pos);
        if (anchor < 0) return false;

        // Find the object { ... } bounds
        int objOpen = json.indexOf('{', anchor);
        if (objOpen < 0) return false;

        int i = objOpen + 1, depth = 1;
        while (i < json.size() && depth > 0) {
            const QChar ch = json.at(i++);
            if (ch == '\"') {
                while (i < json.size()) {
                    if (json.at(i) == '\\') { i += 2; continue; }
                    if (json.at(i) == '\"') { ++i; break; }
                    ++i;
                }
            }
            else if (ch == '{') {
                ++depth;
            }
            else if (ch == '}') {
                --depth;
            }
        }
        if (depth != 0) return false;
        const int objClose = i; // one past '}'

        const QString section = json.mid(objOpen, objClose - objOpen);
        const bool teamOk = section.contains(QRegularExpression(QStringLiteral("\"TEAM\"\\s*:\\s*%1").arg(team)));
        const bool typeOk = section.contains(QRegularExpression(QStringLiteral("\"TYPE\"\\s*:\\s*%1").arg(type)));
        if (!teamOk || !typeOk) { pos = objClose; continue; }

        // Optional list name filter (if the caller provided one)
        if (!listNameOptional.isEmpty()) {
            QRegularExpression nameRe(QStringLiteral("\"NAME\"\\s*:\\s*") +
                QRegularExpression::escape(jsonQuote(listNameOptional)));
            if (!section.contains(nameRe)) { pos = objClose; continue; }
        }

        // 2) Inside this object: find PURCHASE_ITEMS:[ ... ]
        QRegularExpression itemsRe(QStringLiteral("\"PURCHASE_ITEMS\"\\s*:\\s*\\["));
        auto m = itemsRe.match(section);
        if (!m.hasMatch()) { pos = objClose; continue; }

        arrStart = objOpen + m.capturedEnd();

        // Find matching ']'
        int j = arrStart, bDepth = 1;
        while (j < json.size() && bDepth > 0) {
            const QChar ch2 = json.at(j++);
            if (ch2 == '\"') {
                while (j < json.size()) {
                    if (json.at(j) == '\\') { j += 2; continue; }
                    if (json.at(j) == '\"') { ++j; break; }
                    ++j;
                }
            }
            else if (ch2 == '[') {
                ++bDepth;
            }
            else if (ch2 == ']') {
                --bDepth;
            }
        }
        if (bDepth != 0) return false;
        arrEnd = j - 1;
        break;
    }

    // 3) Indentation levels: figure base indent of the line with '['
    int lineStart = json.lastIndexOf('\n', arrStart);
    QString baseIndent;
    if (lineStart >= 0) {
        int k = lineStart + 1;
        while (k < json.size() && (json.at(k) == ' ' || json.at(k) == '\t')) {
            baseIndent += json.at(k); ++k;
        }
    }

    // Body between '[' and ']'
    const QString arrBody = json.mid(arrStart, arrEnd - arrStart);

    // Whitespace-trimmed tail to decide if the array is empty
    int tail = arrBody.size();
    while (tail > 0 && (arrBody[tail - 1] == ' ' || arrBody[tail - 1] == '\t' ||
        arrBody[tail - 1] == '\n' || arrBody[tail - 1] == '\r')) {
        --tail;
    }
    const bool isEmpty = (tail == 0);

    // Element indent:
    //  - If empty array, first element uses baseIndent + "\t"
    //  - If not empty, sniff the indent of the FIRST existing element and use it verbatim
    QString elemIndent = baseIndent + "\t";
    if (!isEmpty) {
        QRegularExpression firstElemIndentRe(QStringLiteral("\n([ \\t]*)\\{"));
        auto mIndent = firstElemIndentRe.match(arrBody);
        if (mIndent.hasMatch()) {
            elemIndent = mIndent.captured(1);   // match existing elements exactly
        }
    }

    // Helpers to render the ALT_* arrays with closing ']' aligned to the key line
    auto arr3i = [&](const QVector<int>& xs) {
        const QString valIndent = elemIndent + "\t";
        const QString closeIndent = elemIndent;
        return QStringLiteral("[\n") + valIndent + QString::number(xs.value(0, 0)) + QStringLiteral(",\n") +
            valIndent + QString::number(xs.value(1, 0)) + QStringLiteral(",\n") +
            valIndent + QString::number(xs.value(2, 0)) + QStringLiteral("\n") +
            closeIndent + QStringLiteral("]");
        };
    auto arr3s = [&](const QVector<QString>& xs) {
        const QString valIndent = elemIndent + "\t";
        const QString closeIndent = elemIndent;
        auto q = [](const QString& s) { return jsonQuote(canonEmpty(s)); };
        return QStringLiteral("[\n") + valIndent + q(xs.value(0)) + QStringLiteral(",\n") +
            valIndent + q(xs.value(1)) + QStringLiteral(",\n") +
            valIndent + q(xs.value(2)) + QStringLiteral("\n") +
            closeIndent + QStringLiteral("]");
        };

    // 4) Item JSON (braces and keys at elemIndent)
    const QString itemText =
        elemIndent + "{\n" +
        elemIndent + "\t\"COST\": " + QString::number(it.cost) + ",\n" +
        elemIndent + "\t\"PRESET_ID\": " + QString::number(it.presetId) + ",\n" +
        elemIndent + "\t\"STRING_ID\": " + QString::number(it.stringId) + ",\n" +
        elemIndent + "\t\"TEXTURE\": " + jsonQuote(it.texture) + ",\n" +
        elemIndent + "\t\"TECH_LEVEL\": " + QString::number(it.techLevel) + ",\n" +
        elemIndent + "\t\"SPECIAL_TECH_NUMBER\": " + QString::number(it.specialTechNumber) + ",\n" +
        elemIndent + "\t\"UNIT_LIMIT\": " + QString::number(it.unitLimit) + ",\n" +
        elemIndent + "\t\"FACTORY\": " + QString::number(it.factory) + ",\n" +
        elemIndent + "\t\"TECH_BUILDING\": " + QString::number(it.techBuilding) + ",\n" +
        elemIndent + "\t\"FACTORY_NOT_REQUIRED\": " + (it.factoryNotRequired ? QStringLiteral("true") : QStringLiteral("false")) + ",\n" +
        elemIndent + "\t\"ALT_PRESETIDS\": " + arr3i(it.altPresetIds) + ",\n" +
        elemIndent + "\t\"ALT_TEXTURES\": " + arr3s(it.altTextures) + "\n" +
        elemIndent + "}";

    // 5) Insert at end (or as first element)
    const int insertPos = isEmpty ? arrStart : (arrStart + tail);
    const QString insertText = isEmpty
        ? (QStringLiteral("\n") + itemText + QStringLiteral("\n") + baseIndent)
        : (QStringLiteral(",\n") + itemText);

    json.insert(insertPos, insertText);

    // Reformat only the just-inserted object so ALT_* arrays get the desired indent
    {
        const int objStart = insertPos + (isEmpty ? 1 : 2); // skip leading "\n" or ",\n"
        const int objLen = itemText.size();
        QString justInserted = json.mid(objStart, objLen);
        normalizeTripleArraysIndent(justInserted);
        json.replace(objStart, objLen, justInserted);
    }
    return true;
}


