#include "PackageTestSupport.h"

#include <array>
#include <iostream>

namespace pkgtest {

namespace {

using uirepo::pkg::EntryKind;
using uirepo::pkg::Manifest;
using uirepo::pkg::PackageBuilder;
using uirepo::pkg::ReadOptions;
using uirepo::pkg::ReadResult;
using uirepo::pkg::Status;
using uirepo::pkg::Version;

void expectRaw(const RawSpec& spec, PackageError expected, const std::string& name, const ReadOptions& options = {}) {
    requireError(read(buildRaw(spec), options).status, expected, name);
}

void expectBytes(const Bytes& bytes, PackageError expected, const std::string& name, const ReadOptions& options = {}) {
    requireError(read(bytes, options).status, expected, name);
}

RawSpec withManifest(Bytes manifest) {
    RawSpec spec = basicSpec();
    spec.manifest = std::move(manifest);
    return spec;
}

Bytes manifestWith(const std::vector<std::pair<std::uint16_t, Bytes>>& records) {
    Bytes out;
    for (const auto& [tag, value] : records) {
        tlv(out, tag, value);
    }
    return out;
}

Bytes u32Value(std::uint32_t value) {
    Bytes out;
    putLe(out, value, 4);
    return out;
}

Bytes depsValue(const std::vector<std::string>& ids) {
    Bytes value;
    putLe(value, ids.size(), 4);
    for (const std::string& id : ids) {
        putLe(value, id.size(), 2);
        value.insert(value.end(), id.begin(), id.end());
        putLe(value, 1, 4);
        putLe(value, 0, 4);
        putLe(value, 0, 4);
    }
    return value;
}

using Record = std::pair<std::uint16_t, Bytes>;

std::vector<Record> goodRecords() {
    return {
        {0x8001, bytesOf("raw.test")},
        {0x8002, versionValue(1, 2, 3)},
        {0x8003, bytesOf("documents/main")},
        {0x8004, u32Value(1)},
    };
}

std::vector<Record> recordsWith(Record extra) {
    std::vector<Record> records = goodRecords();
    records.push_back(std::move(extra));
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.first < b.first; });
    return records;
}

void replaceRecord(std::vector<Record>& records, std::uint16_t tag, Bytes value) {
    for (Record& record : records) {
        if (record.first == tag) {
            record.second = std::move(value);
            return;
        }
    }
    fail("test bug: record not found");
}

void testIndependentLayout() {
    const Bytes raw = buildRaw(basicSpec());
    Manifest manifest;
    manifest.id = "raw.test";
    manifest.version = Version{1, 2, 3};
    manifest.entryDocument = "documents/main";
    PackageBuilder builder(manifest);
    require(builder.add(EntryKind::Asset, "assets/blob.bin", Bytes{0, 1, 2, 3, 4}).ok(), "layout add asset");
    require(builder.add(EntryKind::Document, "documents/main", bytesOf("root-document")).ok(), "layout add document");
    require(builder.add(EntryKind::Style, "styles/theme", bytesOf("style")).ok(), "layout add style");
    Bytes built;
    require(builder.build(built).ok(), "layout build");
    require(raw.size() == built.size(), "writer and independent layout disagree on size");

    RawSpec sorted = basicSpec();
    std::sort(sorted.entries.begin(), sorted.entries.end(), [](const RawEntry& a, const RawEntry& b) { return a.path < b.path; });
    require(buildRaw(sorted) == built, "writer output differs from the independently assembled layout in the format document");
    require(read(built).status.ok(), "independent layout does not read");
    expectRaw(basicSpec(), PackageError::UnsortedEntries, "unsorted entries");
}

void testHeader() {
    const Bytes good = buildRaw([] {
        RawSpec s = basicSpec();
        std::sort(s.entries.begin(), s.entries.end(), [](const RawEntry& a, const RawEntry& b) { return a.path < b.path; });
        return s;
    }());
    require(read(good).status.ok(), "baseline package must read");

    expectBytes({}, PackageError::Truncated, "empty input");
    expectBytes(Bytes{0x89}, PackageError::Truncated, "one byte");
    expectBytes(Bytes(good.begin(), good.begin() + 79), PackageError::Truncated, "one byte short of the minimum size");
    for (std::size_t length = 0; length < good.size(); ++length) {
        expectBytes(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length)), PackageError::Truncated, "prefix of length " + std::to_string(length));
    }

    Bytes bad = good;
    bad[0] ^= 0xFF;
    expectBytes(bad, PackageError::BadMagic, "bad magic");
    expectBytes(Bytes(100, 0), PackageError::BadMagic, "zero filled input");
    expectBytes(Bytes{'P', 'K', 3, 4, 0, 0, 0, 0, 0}, PackageError::BadMagic, "other file type");

    RawSpec spec = basicSpec();
    std::sort(spec.entries.begin(), spec.entries.end(), [](const RawEntry& a, const RawEntry& b) { return a.path < b.path; });
    const RawSpec sortedSpec = spec;

    spec.major = 0;
    expectRaw(spec, PackageError::BadVersion, "major zero");
    spec = sortedSpec;
    spec.major = 2;
    expectRaw(spec, PackageError::UnsupportedMajor, "newer major");
    spec.major = 65535;
    expectRaw(spec, PackageError::UnsupportedMajor, "far future major");
    spec = sortedSpec;
    spec.flags = 1;
    expectRaw(spec, PackageError::UnsupportedFlags, "unknown flag");
    spec = sortedSpec;
    spec.reserved = 1;
    expectRaw(spec, PackageError::ReservedNonZero, "reserved header field");
    spec = sortedSpec;
    spec.headerCrc = 0x12345678;
    expectRaw(spec, PackageError::HeaderChecksum, "header crc");

    bad = good;
    bad[16] ^= 0x01;
    expectBytes(bad, PackageError::HeaderChecksum, "header field changed without resealing");

    bad = good;
    bad.push_back(0);
    expectBytes(bad, PackageError::TrailingData, "one trailing byte");
    bad.insert(bad.end(), 1000, 0xAB);
    expectBytes(bad, PackageError::TrailingData, "trailing garbage");

    spec = sortedSpec;
    spec.total = good.size() + 8;
    expectRaw(spec, PackageError::Truncated, "declared size larger than the data");
    spec.total = good.size() - 8;
    expectRaw(spec, PackageError::TrailingData, "declared size smaller than the data");
    spec.total = 0;
    expectRaw(spec, PackageError::TrailingData, "declared size zero");
    spec.total = ~std::uint64_t{0};
    expectRaw(spec, PackageError::Truncated, "declared size maximal");

    spec = sortedSpec;
    spec.manifestSizeField = 0xFFFFFFFFu;
    expectRaw(spec, PackageError::LimitExceeded, "manifest size maximal");
    spec.manifestSizeField = static_cast<std::uint32_t>(spec.manifest.size() + 1000);
    expectRaw(spec, PackageError::SectionOutOfRange, "manifest larger than the package");
    spec = sortedSpec;
    spec.entryCountField = 0xFFFFFFFFu;
    expectRaw(spec, PackageError::LimitExceeded, "entry count maximal");
    spec.entryCountField = 1000;
    expectRaw(spec, PackageError::SectionOutOfRange, "entry table larger than the package");
    spec = sortedSpec;
    spec.pathBlobField = 0xFFFFFFFFu;
    expectRaw(spec, PackageError::LimitExceeded, "path table maximal");
    spec.pathBlobField = 100;
    require(!read(buildRaw(spec)).status.ok(), "inflated path table must be rejected");
}

void testVersionPolicy() {
    RawSpec spec = basicSpec();
    std::sort(spec.entries.begin(), spec.entries.end(), [](const RawEntry& a, const RawEntry& b) { return a.path < b.path; });
    const RawSpec base = spec;

    spec.minor = 3;
    spec.manifest = manifestWith(recordsWith({0x0100, bytesOf("future")}));
    const auto accepted = read(buildRaw(spec));
    require(accepted.status.ok(), "newer minor with an optional unknown field must be accepted: " + accepted.status.detail);
    require(accepted.package.newerMinor && accepted.package.formatMinor == 3, "newer minor must be reported");
    require(accepted.package.manifest.id == "raw.test", "known fields must still be read from a newer minor");

    spec.manifest = manifestWith(recordsWith({0x8100, bytesOf("future")}));
    expectRaw(spec, PackageError::UnknownManifestField, "newer minor with a critical unknown field");

    spec = base;
    spec.manifest = manifestWith(recordsWith({0x0100, bytesOf("future")}));
    expectRaw(spec, PackageError::UnknownManifestField, "same minor with an unknown optional field");
    spec.manifest = manifestWith(recordsWith({0x8100, bytesOf("future")}));
    expectRaw(spec, PackageError::UnknownManifestField, "same minor with a critical unknown field");

    ReadOptions newerReader;
    newerReader.supportedMinor = 2;
    for (const std::uint16_t minor : std::array<std::uint16_t, 3>{0, 1, 2}) {
        spec = base;
        spec.minor = minor;
        const auto result = read(buildRaw(spec), newerReader);
        require(result.status.ok() && !result.package.newerMinor, "older minor must be accepted by a newer reader");
    }
    spec = base;
    spec.minor = 3;
    const auto result = read(buildRaw(spec), newerReader);
    require(result.status.ok() && result.package.newerMinor, "a minor newer than the reader is accepted when fully understood");
    spec.minor = 3;
    spec.major = 2;
    expectRaw(spec, PackageError::UnsupportedMajor, "newer major is refused whatever the minor");
}

void testManifest() {
    RawSpec spec = basicSpec();
    std::sort(spec.entries.begin(), spec.entries.end(), [](const RawEntry& a, const RawEntry& b) { return a.path < b.path; });
    const RawSpec base = spec;

    spec.manifestCrc = 1;
    expectRaw(spec, PackageError::ManifestChecksum, "manifest crc");
    Bytes bad = buildRaw(base);
    bad[48] ^= 0x01;
    reseal(bad);
    bad[36] ^= 0xFF;
    expectBytes(bad, PackageError::HeaderChecksum, "manifest crc field tampered without resealing the header");

    for (std::size_t drop = 0; drop < 4; ++drop) {
        std::vector<Record> records = goodRecords();
        records.erase(records.begin() + static_cast<std::ptrdiff_t>(drop));
        spec = withManifest(manifestWith(records));
        std::sort(spec.entries.begin(), spec.entries.end(), [](const RawEntry& a, const RawEntry& b) { return a.path < b.path; });
        expectRaw(spec, PackageError::ManifestMissingField, "missing required field " + std::to_string(drop));
    }
    spec = withManifest({});
    expectRaw(spec, PackageError::ManifestMissingField, "empty manifest");

    const auto manifestCase = [&](std::vector<Record> records, PackageError expected, const std::string& name) {
        RawSpec s = base;
        s.manifest = manifestWith(records);
        expectRaw(s, expected, name);
    };
    std::vector<Record> records = goodRecords();
    records.push_back(records.back());
    manifestCase(records, PackageError::ManifestMalformed, "duplicate tag");
    records = goodRecords();
    std::swap(records[0], records[1]);
    manifestCase(records, PackageError::ManifestMalformed, "tags out of order");

    spec = base;
    spec.manifest = manifestBytes();
    spec.manifest.push_back(0x01);
    spec.manifest.push_back(0x02);
    expectRaw(spec, PackageError::ManifestMalformed, "truncated record header");
    spec.manifest = manifestBytes();
    putLe(spec.manifest, 0x0100, 2);
    putLe(spec.manifest, 500, 4);
    expectRaw(spec, PackageError::ManifestMalformed, "record length beyond the manifest");

    records = goodRecords();
    replaceRecord(records, 0x8002, Bytes(11, 0));
    manifestCase(records, PackageError::ManifestMalformed, "short version");
    records = goodRecords();
    replaceRecord(records, 0x8004, Bytes(3, 0));
    manifestCase(records, PackageError::ManifestMalformed, "short api version");
    records = goodRecords();
    replaceRecord(records, 0x8004, u32Value(0));
    manifestCase(records, PackageError::ManifestInvalidValue, "zero api version");
    records = goodRecords();
    replaceRecord(records, 0x8001, bytesOf("Upper"));
    manifestCase(records, PackageError::ManifestInvalidValue, "uppercase id");
    records = goodRecords();
    replaceRecord(records, 0x8001, bytesOf(""));
    manifestCase(records, PackageError::ManifestInvalidValue, "empty id");
    records = goodRecords();
    replaceRecord(records, 0x8001, bytesOf(std::string(65, 'a')));
    manifestCase(records, PackageError::ManifestInvalidValue, "id too long");
    records = goodRecords();
    replaceRecord(records, 0x8003, bytesOf("styles/theme"));
    manifestCase(records, PackageError::ManifestInvalidValue, "entry document outside documents/");
    records = goodRecords();
    replaceRecord(records, 0x8003, bytesOf("documents/../main"));
    manifestCase(records, PackageError::ManifestInvalidValue, "entry document traversal");
    records = goodRecords();
    replaceRecord(records, 0x8003, bytesOf("documents/other"));
    manifestCase(records, PackageError::EntryDocumentMissing, "entry document not in the package");

    manifestCase(recordsWith({0x0001, bytesOf("")}), PackageError::ManifestInvalidValue, "empty display name");
    manifestCase(recordsWith({0x0001, bytesOf("bad\nname")}), PackageError::ManifestInvalidValue, "control character in display name");
    manifestCase(recordsWith({0x0001, bytesOf(std::string(129, 'x'))}), PackageError::ManifestInvalidValue, "long display name");

    manifestCase(recordsWith({0x8005, listValue({})}), PackageError::ManifestInvalidValue, "present but empty capability list");
    manifestCase(recordsWith({0x8005, listValue({"text-layout", "retained-documents"})}), PackageError::ManifestInvalidValue, "unsorted capabilities");
    manifestCase(recordsWith({0x8005, listValue({"text-layout", "text-layout"})}), PackageError::ManifestInvalidValue, "duplicate capabilities");
    manifestCase(recordsWith({0x8005, listValue({"Text"})}), PackageError::ManifestInvalidValue, "bad capability identifier");
    manifestCase(recordsWith({0x0002, listValue({"text-layout"})}), PackageError::Ok, "optional capability list");
    Bytes lying = listValue({"text-layout"});
    store(lying, 0, 5, 4);
    manifestCase(recordsWith({0x8005, lying}), PackageError::ManifestMalformed, "list count larger than its items");
    lying = listValue({"text-layout"});
    lying.push_back(0);
    manifestCase(recordsWith({0x8005, lying}), PackageError::ManifestMalformed, "trailing bytes after a list");
    lying = listValue({"text-layout"});
    store(lying, 4, 500, 2);
    manifestCase(recordsWith({0x8005, lying}), PackageError::ManifestMalformed, "list item longer than its record");
    manifestCase(recordsWith({0x8005, Bytes{1, 0}}), PackageError::ManifestMalformed, "list shorter than its count");

    manifestCase(recordsWith({0x8006, depsValue({"alpha", "beta"})}), PackageError::Ok, "valid dependencies");
    manifestCase(recordsWith({0x8006, depsValue({"beta", "alpha"})}), PackageError::ManifestInvalidValue, "unsorted dependencies");
    manifestCase(recordsWith({0x8006, depsValue({"alpha", "alpha"})}), PackageError::ManifestInvalidValue, "duplicate dependencies");
    manifestCase(recordsWith({0x8006, depsValue({"raw.test"})}), PackageError::ManifestInvalidValue, "self dependency");
    manifestCase(recordsWith({0x8006, depsValue({})}), PackageError::ManifestInvalidValue, "present but empty dependency list");
    Bytes shortDeps = depsValue({"alpha"});
    shortDeps.pop_back();
    manifestCase(recordsWith({0x8006, shortDeps}), PackageError::ManifestMalformed, "dependency missing version bytes");

    manifestCase(recordsWith({0x8005, listValue({"quantum-ui"})}), PackageError::UnsupportedCapability, "unknown required capability");
    manifestCase(recordsWith({0x0002, listValue({"quantum-ui"})}), PackageError::Ok, "unknown optional capability is allowed");
    manifestCase(recordsWith({0x8005, listValue({"text-layout"})}), PackageError::Ok, "known required capability");

    ReadOptions options;
    const std::array<std::string_view, 1> onlyOne = {"focus-lease"};
    options.hostCapabilities = onlyOne;
    RawSpec capSpec = base;
    capSpec.manifest = manifestWith(recordsWith({0x8005, listValue({"text-layout"})}));
    expectRaw(capSpec, PackageError::UnsupportedCapability, "capability outside the host list", options);
    capSpec.manifest = manifestWith(recordsWith({0x8005, listValue({"focus-lease"})}));
    expectRaw(capSpec, PackageError::Ok, "capability inside the host list", options);

    RawSpec apiSpec = base;
    records = goodRecords();
    replaceRecord(records, 0x8004, u32Value(5));
    apiSpec.manifest = manifestWith(records);
    options = {};
    options.hostApiVersion = 2;
    expectRaw(apiSpec, PackageError::IncompatibleHost, "package needs a newer host api", options);
    options.hostApiVersion = 5;
    expectRaw(apiSpec, PackageError::Ok, "host api equal to the requirement", options);
    options.hostApiVersion = 0;
    expectRaw(apiSpec, PackageError::Ok, "host api unchecked", options);
}

void testTable() {
    const auto entries = [](std::vector<RawEntry> list) {
        RawSpec spec;
        spec.entries = std::move(list);
        return spec;
    };
    const RawEntry doc{.kind = 1, .path = "documents/main", .data = bytesOf("doc")};

    expectRaw(entries({}), PackageError::EntryDocumentMissing, "no entries");
    RawSpec spec = entries({doc});
    expectRaw(spec, PackageError::Ok, "single entry");

    for (const std::uint8_t kind : std::array<std::uint8_t, 3>{0, 5, 255}) {
        RawEntry bad = doc;
        bad.kind = kind;
        expectRaw(entries({bad}), PackageError::UnknownEntryKind, "kind " + std::to_string(kind));
    }
    RawEntry padded = doc;
    padded.recordPad = 1;
    expectRaw(entries({padded}), PackageError::ReservedNonZero, "record padding");

    expectRaw(entries({doc, doc}), PackageError::DuplicatePath, "duplicate path");
    expectRaw(
        entries({doc, RawEntry{.kind = 4, .path = "assets/a", .data = {}}}), PackageError::UnsortedEntries, "entries not sorted");

    for (const char* path : {"documents/../a", "/documents/a", "documents\\a", "documents/Main", "documents/./a", "documents//a",
                             "documents/a/", "documents/con", "documents/a b", "documents/.a", "", "documents/a.", "../documents/a"}) {
        RawEntry bad{.kind = 1, .path = path, .data = {}};
        expectRaw(entries({bad, doc}), PackageError::BadPath, std::string("path '") + path + "'");
    }
    expectRaw(
        entries({RawEntry{.kind = 1, .path = "documents/" + std::string(300, 'a'), .data = {}}, doc}),
        PackageError::BadPath,
        "overlong path");
    const auto styleEntry = [](std::string path) { return RawEntry{.kind = 2, .path = std::move(path), .data = {}}; };
    expectRaw(
        entries({doc, styleEntry("styles/a"), styleEntry("styles/a-x"), styleEntry("styles/a.x"), styleEntry("styles/a/b")}),
        PackageError::FileDirectoryConflict,
        "file and directory share a name, not adjacent");
    expectRaw(entries({doc, styleEntry("styles/a"), styleEntry("styles/a/b")}), PackageError::FileDirectoryConflict, "file and directory share a name");
    expectRaw(
        entries({doc, styleEntry("styles/a"), styleEntry("styles/a-x"), styleEntry("styles/a.x"), styleEntry("styles/ab")}),
        PackageError::Ok,
        "names sharing only a prefix");

    const std::string hostilePath = std::string("documents/a\n\x01\xFF") + std::string(300, 'z');
    const Status hostile = read(buildRaw(entries({RawEntry{.kind = 1, .path = hostilePath, .data = {}}, doc}))).status;
    requireError(hostile, PackageError::BadPath, "hostile path bytes");
    require(
        std::all_of(hostile.detail.begin(), hostile.detail.end(), [](char c) { return c >= 0x20 && c < 0x7F; }) && hostile.detail.size() < 200,
        "BadPath detail must be printable and bounded");
    expectRaw(entries({RawEntry{.kind = 2, .path = "documents/a", .data = {}}, doc}), PackageError::KindPathMismatch, "style outside styles/");
    expectRaw(entries({RawEntry{.kind = 4, .path = "documents/a", .data = {}}, doc}), PackageError::KindPathMismatch, "asset outside assets/");

    spec = entries({doc, RawEntry{.kind = 2, .path = "styles/s", .data = bytesOf("s")}});
    spec.entries[1].pathOffset = 0;
    expectRaw(spec, PackageError::PathBlobMismatch, "path offset repeats an earlier path");
    spec = entries({doc});
    spec.entries[0].pathLength = 1000;
    expectRaw(spec, PackageError::PathBlobMismatch, "path length beyond the blob");
    spec = entries({doc});
    spec.entries[0].pathLength = static_cast<std::uint32_t>(doc.path.size() - 1);
    expectRaw(spec, PackageError::PathBlobMismatch, "unreferenced path bytes");

    const auto three = [&] {
        return entries({
            RawEntry{.kind = 4, .path = "assets/a", .data = bytesOf("aaaaaaaaaaaaaa")},
            RawEntry{.kind = 1, .path = "documents/main", .data = bytesOf("doc")},
            RawEntry{.kind = 2, .path = "styles/s", .data = bytesOf("sssss")},
        });
    };
    expectRaw(three(), PackageError::Ok, "three entries baseline");
    const std::uint64_t base = [&] {
        const Bytes bytes = buildRaw(three());
        const std::uint64_t tableOffset = align8(48 + three().manifest.size());
        return load32(bytes, static_cast<std::size_t>(tableOffset + 16));
    }();

    spec = three();
    spec.entries[1].offset = base;
    expectRaw(spec, PackageError::EntryOverlap, "second entry starts where the first starts");
    spec = three();
    spec.entries[2].offset = 0;
    expectRaw(spec, PackageError::EntryOverlap, "entry points into the header");
    spec = three();
    spec.entries[0].offset = base + 8;
    expectRaw(spec, PackageError::EntryGap, "entry skips ahead");
    spec = three();
    spec.entries[2].offset = 1u << 20;
    expectRaw(spec, PackageError::EntryOutOfRange, "offset beyond the package");
    spec = three();
    spec.entries[1].offset = ~std::uint64_t{0};
    spec.entries[1].size = 2;
    expectRaw(spec, PackageError::EntryOutOfRange, "offset plus size overflows");
    spec = three();
    spec.entries[1].offset = ~std::uint64_t{0} - 7;
    spec.entries[1].size = 16;
    expectRaw(spec, PackageError::EntryOutOfRange, "offset plus size wraps around");
    spec = three();
    spec.entries[1].size = 100'000'000;
    expectRaw(spec, PackageError::EntryOutOfRange, "size beyond the package");
    spec = three();
    spec.entries[1].size = ~std::uint64_t{0};
    expectRaw(spec, PackageError::LimitExceeded, "size beyond the entry limit");
    spec = three();
    spec.entries[2].size = 0;
    spec.entries[2].crc = 0;
    expectRaw(spec, PackageError::EntryGap, "slack before the footer");
    spec = three();
    spec.entries[0].crc = 0xDEADBEEF;
    expectRaw(spec, PackageError::EntryChecksum, "entry crc");
    spec = three();
    spec.entries[0].size = spec.entries[0].data.size() + 1;
    expectRaw(spec, PackageError::EntryChecksum, "entry size grown over its padding");
    spec = three();
    spec.sealFooter = false;
    expectRaw(spec, PackageError::PackageDigest, "footer digest");
    spec = three();
    spec.paddingFill = 0xAA;
    expectRaw(spec, PackageError::NonZeroPadding, "padding filled with ones");
}

void poke(const Bytes& bytes, std::size_t at, PackageError expected, const std::string& name) {
    Bytes copy = bytes;
    copy[at] = 0x5A;
    reseal(copy);
    expectBytes(copy, expected, name);
}

void testPaddingAndIntegrity() {
    RawSpec spec = basicSpec();
    std::sort(spec.entries.begin(), spec.entries.end(), [](const RawEntry& a, const RawEntry& b) { return a.path < b.path; });
    const Bytes good = buildRaw(spec);
    require(read(good).status.ok(), "padding baseline");

    const std::size_t manifestSize = spec.manifest.size();
    const std::size_t tableOffset = static_cast<std::size_t>(align8(48 + manifestSize));
    const std::size_t pathOffset = tableOffset + spec.entries.size() * 32;
    std::size_t pathBytes = 0;
    for (const RawEntry& e : spec.entries) {
        pathBytes += e.path.size();
    }
    const std::size_t lastRecord = tableOffset + (spec.entries.size() - 1) * 32;
    const std::size_t lastOffset = static_cast<std::size_t>(load32(good, lastRecord + 16));
    const std::size_t lastSize = static_cast<std::size_t>(load32(good, lastRecord + 24));
    const std::size_t firstOffset = static_cast<std::size_t>(load32(good, tableOffset + 16));
    const std::size_t firstSize = static_cast<std::size_t>(load32(good, tableOffset + 24));

    require(manifestSize % 8 != 0 && pathBytes % 8 != 0 && firstSize % 8 != 0 && lastSize % 8 != 0, "test data must leave padding in every region");
    poke(good, 48 + manifestSize, PackageError::NonZeroPadding, "padding after the manifest");
    poke(good, pathOffset + pathBytes, PackageError::NonZeroPadding, "padding after the path table");
    poke(good, firstOffset + firstSize, PackageError::NonZeroPadding, "padding between entries");
    poke(good, lastOffset + lastSize, PackageError::NonZeroPadding, "padding before the footer");

    poke(good, firstOffset, PackageError::EntryChecksum, "entry data flipped and sealed");

    Bytes bad = good;
    bad[good.size() - 1] ^= 0x01;
    expectBytes(bad, PackageError::PackageDigest, "last digest byte flipped");
    bad = good;
    bad[good.size() - 33] ^= 0x01;
    expectBytes(bad, PackageError::NonZeroPadding, "last padding byte flipped");
    bad = good;
    bad[tableOffset + 3] ^= 0x01;
    expectBytes(bad, PackageError::PathBlobMismatch, "table byte flipped");
}

void verifyAccepted(const Bytes& bytes, const ReadResult& result, const std::string& label) {
    using namespace uirepo::pkg;
    require(result.status.ok(), "verifyAccepted needs an accepted package");
    const auto prefix = std::span<const std::uint8_t>(bytes.data(), bytes.size() - kFooterSize);
    require(result.package.digest == sha256(prefix), label + ": accepted package whose digest does not validate");
    require(std::equal(result.package.digest.begin(), result.package.digest.end(), bytes.end() - static_cast<std::ptrdiff_t>(kFooterSize)), label + ": digest differs from footer");
    require(result.package.find(result.package.manifest.entryDocument) != nullptr, label + ": accepted package lost its entry document");
    for (std::size_t i = 0; i < result.package.entries.size(); ++i) {
        const Entry& entry = result.package.entries[i];
        require(crc32(entry.data) == entry.crc32, label + ": accepted entry whose crc does not validate");
        require(isCanonicalPath(entry.path), label + ": accepted a non canonical path");
        require(entry.data.empty() || (entry.data.data() >= bytes.data() && entry.data.data() + entry.data.size() <= bytes.data() + bytes.size()), label + ": entry data escapes the input");
        require(i == 0 || result.package.entries[i - 1].path < entry.path, label + ": accepted unsorted entries");
    }
}

Bytes fuzzSubject() {
    Manifest manifest;
    manifest.id = "fuzz.pkg";
    manifest.displayName = "Fuzz";
    manifest.entryDocument = "documents/main";
    manifest.requiredCapabilities = {"text-layout"};
    manifest.dependencies = {uirepo::pkg::Dependency{"base.lib", Version{1, 0, 0}}};
    PackageBuilder builder(manifest);
    require(builder.add(EntryKind::Document, "documents/main", bytesOf("<doc>")).ok(), "fuzz add document");
    require(builder.add(EntryKind::Style, "styles/s", bytesOf("sty")).ok(), "fuzz add style");
    require(builder.add(EntryKind::Asset, "assets/a", Bytes{1, 2, 3, 4, 5, 6, 7, 8, 9}).ok(), "fuzz add asset");
    Bytes out;
    require(builder.build(out).ok(), "fuzz build");
    return out;
}

void testMutationLoops() {
    const Bytes subject = fuzzSubject();
    require(read(subject).status.ok(), "fuzz subject must read");

    std::size_t rawAccepted = 0;
    std::size_t sealedAccepted = 0;
    std::size_t sealedRejected = 0;
    for (std::size_t i = 0; i < subject.size(); ++i) {
        for (const std::uint8_t mask : std::array<std::uint8_t, 4>{0x01, 0x10, 0x80, 0xFF}) {
            Bytes mutated = subject;
            mutated[i] ^= mask;
            const ReadResult raw = read(mutated);
            if (raw.status.ok()) {
                ++rawAccepted;
                verifyAccepted(mutated, raw, "raw flip at " + std::to_string(i));
            }
            reseal(mutated);
            const ReadResult sealed = read(mutated);
            if (sealed.status.ok()) {
                ++sealedAccepted;
                verifyAccepted(mutated, sealed, "sealed flip at " + std::to_string(i));
            } else {
                ++sealedRejected;
            }
        }
    }
    require(rawAccepted == 0, "a flipped byte was accepted although the package digest covers every byte");
    require(sealedRejected > sealedAccepted, "resealed mutations should mostly be rejected by structural checks");

    for (std::size_t i = 0; i < subject.size(); ++i) {
        Bytes erased = subject;
        erased.erase(erased.begin() + static_cast<std::ptrdiff_t>(i));
        const ReadResult dropped = read(erased);
        require(!dropped.status.ok(), "a package with a deleted byte was accepted");
        reseal(erased);
        const ReadResult resealed = read(erased);
        if (resealed.status.ok()) {
            verifyAccepted(erased, resealed, "erase at " + std::to_string(i));
        }
        Bytes inserted = subject;
        inserted.insert(inserted.begin() + static_cast<std::ptrdiff_t>(i), 0x00);
        require(!read(inserted).status.ok(), "a package with an inserted byte was accepted");
        reseal(inserted);
        const ReadResult grown = read(inserted);
        if (grown.status.ok()) {
            verifyAccepted(inserted, grown, "insert at " + std::to_string(i));
        }
    }

    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    std::size_t accepted = 0;
    for (std::size_t round = 0; round < 40000; ++round) {
        Bytes mutated = subject;
        const std::size_t edits = 1 + next() % 4;
        for (std::size_t e = 0; e < edits; ++e) {
            mutated[next() % mutated.size()] = static_cast<std::uint8_t>(next());
        }
        if (next() % 8 == 0) {
            mutated.resize(next() % (mutated.size() + 1));
        }
        if (next() % 2 == 0) {
            reseal(mutated);
        }
        const ReadResult result = read(mutated);
        if (result.status.ok()) {
            ++accepted;
            verifyAccepted(mutated, result, "random mutation " + std::to_string(round));
        }
    }
    std::cout << "mutation loops: raw accepted " << rawAccepted << ", sealed accepted " << sealedAccepted << ", sealed rejected "
              << sealedRejected << ", random accepted " << accepted << '\n';
}

}  // namespace

void runMalformedTests() {
    testIndependentLayout();
    testHeader();
    testVersionPolicy();
    testManifest();
    testTable();
    testPaddingAndIntegrity();
    testMutationLoops();
}

}  // namespace pkgtest
