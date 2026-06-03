// Host generator for bare-metal protoacc tests.
//
// Per test, emits <test>_data.h containing:
//  - descriptor tables (16-byte aligned, incl. trailing is_submessage words;
//    vptr + nested pointers zeroed and patched at runtime)
//  - GOLDEN_OBJ: the serializer-input C++ message object image (plus
//    string/bytes blocks, RepeatedField Rep blocks, nested sub-objects),
//    pointers zeroed and patched at runtime
//  - serialized_golden: CPU-protobuf serialization of that object
//  - patch_descriptors()/patch_objects() to wire runtime pointers
//
// Layouts (protobuf 3.11.2, both x86-64/rv64 LP64):
//   string/bytes field = ptr -> std::string{data_ptr, size, cap}
//   numeric repeated   = {cur, total, Rep*} -> Rep{Arena*, elems[]}
//   ptr repeated       = {cur, total, Rep*} -> Rep{alloc, void*[]} -> str blocks
//   submessage field   = ptr -> child object
//
// Field values mirror gen-primitive-tests.py.

#include "primitives.pb.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>

using namespace std;
using google::protobuf::Message;
using google::protobuf::FieldDescriptor;
using google::protobuf::Reflection;

static FILE* out;

// ----------------------------------------------------------------- descriptors
static map<const uint64_t*, string> table_names;
static vector<pair<string, const uint64_t*>> table_order;
static int tbl_counter;

static size_t nrel_fields(const uint64_t* d) {
    return (size_t)((uint32_t)(d[3] & 0xffffffffULL) - (uint32_t)(d[3] >> 32) + 1);
}
static size_t descr_field_words(const uint64_t* d) { return nrel_fields(d) * 2; }
static size_t descr_len(const uint64_t* d) {
    size_t nrel = nrel_fields(d);
    return 4 + nrel * 2 + (((nrel + 31) / 32) + 1) / 2; // hdr + fields + is_submessage
}

static string register_table(const uint64_t* d) {
    auto it = table_names.find(d);
    if (it != table_names.end()) return it->second;
    string name = "descr_" + to_string(tbl_counter++);
    table_names[d] = name;
    size_t fend = 4 + descr_field_words(d);
    for (size_t i = 4; i + 1 < fend; i += 2)
        if (d[i + 1]) register_table((const uint64_t*)d[i + 1]);
    table_order.push_back({name, d});
    return name;
}

static void emit_tables() {
    for (auto& pr : table_order) {
        const uint64_t* d = pr.second;
        size_t n = descr_len(d), fend = 4 + descr_field_words(d);
        fprintf(out, "__attribute__((aligned(16))) static uint64_t %s[%zu] = {\n", pr.first.c_str(), n);
        for (size_t i = 0; i < n; i++) {
            uint64_t v = d[i];
            if (i == 0) v = 0;                                       // vptr
            else if (i >= 5 && i < fend && ((i - 4) % 2 == 1)) v = 0; // nested ptr
            fprintf(out, "  0x%016llxULL,\n", (unsigned long long)v);
        }
        fprintf(out, "};\n");
    }
    fprintf(out, "static void patch_descriptors(void) {\n");
    for (auto& pr : table_order) {
        const uint64_t* d = pr.second;
        size_t fend = 4 + descr_field_words(d);
        for (size_t i = 4; i + 1 < fend; i += 2)
            if (d[i + 1])
                fprintf(out, "  %s[%zu] = (uint64_t)(uintptr_t)%s;\n",
                        pr.first.c_str(), i + 1, table_names[(const uint64_t*)d[i + 1]].c_str());
    }
    fprintf(out, "}\n");
}

// ------------------------------------------------------------------- objects
// Every emitted entity is a 16-byte-aligned byte blob + list of pointer slots.
struct Blob {
    string name;
    vector<unsigned char> bytes;
    struct Patch { size_t off; string target; size_t addend; };
    vector<Patch> patches;
};
static vector<Blob> blobs;
static int blob_counter;

static string emit_object(const Message& m, const uint64_t* descr);

static string add_blob(const string& prefix, const void* data, size_t len) {
    Blob b;
    b.name = prefix + "_" + to_string(blob_counter++);
    b.bytes.assign((const unsigned char*)data, (const unsigned char*)data + len);
    blobs.push_back(b);
    return blobs.back().name;
}

// std::string image: {data_ptr, size, capacity} + the character data.
static string emit_string_block(const string& s) {
    uint64_t hdr[4] = {0, s.size(), s.size(), 0};
    string blk = add_blob("strblk", hdr, sizeof(hdr));
    string data = add_blob("strdata", s.data(), s.size() ? s.size() : 1);
    for (auto& b : blobs) if (b.name == blk) b.patches.push_back({0, data, 0});
    return blk;
}

// Descriptor type codes are protobuf FieldDescriptor::TYPE_* (1..18).
#define T_STRING 9
#define T_MESSAGE 11
#define T_BYTES 12
static size_t elem_size(int t) {
    switch (t) {
        case 2: case 5: case 7: case 13: case 14: case 15: case 17: return 4;
        case 8: return 1;
        default: return 8;
    }
}

static string emit_object(const Message& m, const uint64_t* descr) {
    size_t objsize = descr[1];
    const unsigned char* raw = (const unsigned char*)&m;

    Blob b;
    b.name = "obj_" + to_string(blob_counter++);
    b.bytes.assign(raw, raw + objsize);
    memset(b.bytes.data(), 0, 8); // vptr

    const Reflection* refl = m.GetReflection();
    const google::protobuf::Descriptor* desc = m.GetDescriptor();
    size_t nrel = nrel_fields(descr);
    uint32_t minf = (uint32_t)(descr[3] >> 32);

    size_t self = blobs.size();
    blobs.push_back(b);

    for (size_t i = 0; i < nrel; i++) {
        uint64_t w = descr[4 + 2 * i];
        bool repeated = w >> 63;
        int ctype = (int)((w >> 58) & 0x1f);
        size_t off = w & ((1ULL << 58) - 1);
        const FieldDescriptor* fd = desc->FindFieldByNumber((int)(minf + i));
        if (!fd) continue;

        if (repeated) {
            int n = refl->FieldSize(m, fd);
            // serializer dispatch is hasbits-driven: mark non-empty repeated.
            if (n) ((uint32_t*)(blobs[self].bytes.data() + descr[2]))[(i + 1) / 32] |= 1u << ((i + 1) % 32);
            // descriptor offset points at arena_or_elements_/rep_;
            // current_size_/total_size_ live at off-8.
            const unsigned char* repptr = *(const unsigned char* const*)(raw + off);
            memset(blobs[self].bytes.data() + off, 0, 8);
            if (!n || !repptr) continue;
            if (ctype == T_STRING || ctype == T_BYTES) {
                vector<uint64_t> rep(1 + n, 0);
                rep[0] = (uint64_t)n; // allocated_size
                string rn = add_blob("rep", rep.data(), rep.size() * 8);
                for (int e = 0; e < n; e++) {
                    string blk = emit_string_block(refl->GetRepeatedString(m, fd, e));
                    for (auto& bb : blobs) if (bb.name == rn) bb.patches.push_back({8 + 8 * (size_t)e, blk, 0});
                }
                blobs[self].patches.push_back({off + 8, rn, 0});
            } else {
                // numeric repeated: arena_or_elements_ points directly at the
                // elements; emit a Rep image {arena=0, elems[]} and patch the
                // object's pointer to rep+8 (see patch fixup below).
                size_t es = elem_size(ctype);
                vector<unsigned char> rep(8 + es * n, 0);
                memcpy(rep.data() + 8, repptr, es * n);
                string rn = add_blob("rep", rep.data(), rep.size()); // before patches: may realloc blobs
                blobs[self].patches.push_back({off, rn, 8});
            }
        } else if (ctype == T_STRING || ctype == T_BYTES) {
            memset(blobs[self].bytes.data() + off, 0, 8);
            if (refl->HasField(m, fd)) {
                string blk = emit_string_block(refl->GetString(m, fd));
                blobs[self].patches.push_back({off, blk, 0});
            }
        } else if (ctype == T_MESSAGE) {
            memset(blobs[self].bytes.data() + off, 0, 8);
            if (refl->HasField(m, fd)) {
                string child = emit_object(refl->GetMessage(m, fd), (const uint64_t*)descr[4 + 2 * i + 1]);
                blobs[self].patches.push_back({off, child, 0});
            }
        }
    }
    return blobs[self].name;
}

static void emit_blobs() {
    for (auto& b : blobs) {
        fprintf(out, "__attribute__((aligned(16))) static unsigned char %s[%zu] = {", b.name.c_str(), b.bytes.size());
        for (size_t i = 0; i < b.bytes.size(); i++) {
            if (i % 16 == 0) fprintf(out, "\n  ");
            fprintf(out, "0x%02x,", b.bytes[i]);
        }
        fprintf(out, "\n};\n");
    }
    fprintf(out, "static void patch_objects(void) {\n");
    for (auto& b : blobs)
        for (auto& p : b.patches)
            fprintf(out, "  *(uint64_t*)(%s + %zu) = (uint64_t)(uintptr_t)(%s + %zu);\n",
                    b.name.c_str(), p.off, p.target.c_str(), p.addend);
    fprintf(out, "}\n");
}

static void emit_bytes(const char* name, const string& s) {
    fprintf(out, "static const unsigned char %s[] = {", name);
    for (size_t i = 0; i < s.size(); i++) {
        if (i % 16 == 0) fprintf(out, "\n  ");
        fprintf(out, "0x%02x,", (unsigned char)s[i]);
    }
    fprintf(out, "\n};\n");
}

// ------------------------------------------------------------- value filling
static const uint64_t varintvals[11] = {0ULL, 1ULL, 128ULL, 16384ULL, 2097152ULL,
    268435456ULL, 34359738368ULL, 4398046511104ULL, 562949953421312ULL,
    72057594037927936ULL, 9223372036854775808ULL};

// Test values mirror gen-primitive-tests.py: one helper per protobuf type.
static double   double_val(const string&)  { return 1.0; }
static float    float_val(const string&)   { return 1.0f; }
static bool     bool_val(const string&)    { return true; }
static int32_t  int32_val(const string&)   { return 1; }
static int64_t  int64_val(const string&)   { return 1; }
static uint32_t uint32_val(const string&)  { return 1; }

static string string_val(const string& tname) {
    if (tname.find("very_long") != string::npos) return string(489, 'a');
    if (tname.find("long") != string::npos) return "hello hello hello hello hello hello hello";
    if (tname.find("_15") != string::npos) return "hello hello he";
    return "hello";
}

static uint64_t varint_val(const string& tname) {
    if (tname.rfind("ser10", 0) == 0) return varintvals[10]; // size10B fields_N tests
    size_t p = tname.find("size");
    if (p == string::npos) return 1;
    return varintvals[atoi(tname.substr(p + 4, 2).c_str())];
}

static void fill(Message* m, const string& tname) {
    const google::protobuf::Descriptor* d = m->GetDescriptor();
    const Reflection* r = m->GetReflection();
    for (int i = 0; i < d->field_count(); i++) {
        const FieldDescriptor* f = d->field(i);
        int reps = f->is_repeated() ? 5 : 1;
        for (int e = 0; e < reps; e++) {
            switch (f->cpp_type()) {
                case FieldDescriptor::CPPTYPE_DOUBLE:
                    f->is_repeated() ? r->AddDouble(m, f, double_val(tname)) : r->SetDouble(m, f, double_val(tname)); break;
                case FieldDescriptor::CPPTYPE_FLOAT:
                    f->is_repeated() ? r->AddFloat(m, f, float_val(tname)) : r->SetFloat(m, f, float_val(tname)); break;
                case FieldDescriptor::CPPTYPE_BOOL:
                    f->is_repeated() ? r->AddBool(m, f, bool_val(tname)) : r->SetBool(m, f, bool_val(tname)); break;
                case FieldDescriptor::CPPTYPE_INT32:
                    f->is_repeated() ? r->AddInt32(m, f, int32_val(tname)) : r->SetInt32(m, f, int32_val(tname)); break;
                case FieldDescriptor::CPPTYPE_INT64:
                    f->is_repeated() ? r->AddInt64(m, f, int64_val(tname)) : r->SetInt64(m, f, int64_val(tname)); break;
                case FieldDescriptor::CPPTYPE_UINT32:
                    f->is_repeated() ? r->AddUInt32(m, f, uint32_val(tname)) : r->SetUInt32(m, f, uint32_val(tname)); break;
                case FieldDescriptor::CPPTYPE_UINT64:
                    f->is_repeated() ? r->AddUInt64(m, f, varint_val(tname)) : r->SetUInt64(m, f, varint_val(tname)); break;
                case FieldDescriptor::CPPTYPE_STRING:
                    f->is_repeated() ? r->AddString(m, f, string_val(tname)) : r->SetString(m, f, string_val(tname)); break;
                case FieldDescriptor::CPPTYPE_MESSAGE:
                    fill(r->MutableMessage(m, f), tname); break;
                default: break;
            }
        }
    }
}

// ------------------------------------------------------------------ per test
static void gen_one(const char* tname, const Message& prototype, const uint64_t* descr) {
    string fname = string(tname) + "_data.h";
    out = fopen(fname.c_str(), "w");
    table_names.clear(); table_order.clear(); tbl_counter = 0;
    blobs.clear(); blob_counter = 0;

    Message* m = prototype.New();
    fill(m, tname);
    string golden;
    m->SerializeToString(&golden);

    fprintf(out, "// auto-generated by gen_data.cpp -- do not edit\n#include <stdint.h>\n");
    string root_d = register_table(descr);
    emit_tables();
    string root_o = emit_object(*m, descr);
    emit_blobs();
    emit_bytes("serialized_golden", golden);
    fprintf(out, "#define ROOT_DESCRIPTOR %s\n", root_d.c_str());
    fprintf(out, "#define GOLDEN_OBJ %s\n", root_o.c_str());
    fprintf(out, "#define SERIALIZED_GOLDEN_LEN %zuUL\n", golden.size());
    fprintf(out, "#define TEST_NAME \"%s\"\n", tname);
    fclose(out);
    printf("%s: obj+%zu blobs, golden %zu bytes\n", tname, blobs.size(), golden.size());
    delete m;
}

#define GEN(tname, MSG) \
    gen_one(tname, primitivetests::MSG::default_instance(), \
        primitivetests_FriendStruct_##MSG##_ACCEL_DESCRIPTORS::MSG##_ACCEL_DESCRIPTORS)

// The serializer suite (fields_N) lives in a separate primitives.proto with the
// same file/package name -- the two .pb.cc cannot link together. Build twice:
// default = parser suite; -DSER_FIELDS_TESTS = serializer fields_N suite.
#ifdef SER_FIELDS_TESTS
int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    GEN("ser_fields_1", Paccser_uint64_size01B_fields_1Message);
    GEN("ser_fields_2", Paccser_uint64_size01B_fields_2Message);
    GEN("ser_fields_3", Paccser_uint64_size01B_fields_3Message);
    GEN("ser_fields_4", Paccser_uint64_size01B_fields_4Message);
    GEN("ser_fields_5", Paccser_uint64_size01B_fields_5Message);
    GEN("ser_fields_6", Paccser_uint64_size01B_fields_6Message);
    GEN("ser_fields_7", Paccser_uint64_size01B_fields_7Message);
    GEN("ser_fields_8", Paccser_uint64_size01B_fields_8Message);
    GEN("ser_fields_16", Paccser_uint64_size01B_fields_16Message);
    GEN("ser_fields_32", Paccser_uint64_size01B_fields_32Message);
    GEN("ser_fields_64", Paccser_uint64_size01B_fields_64Message);
    GEN("ser10_fields_1", Paccser_uint64_size10B_fields_1Message);
    GEN("ser10_fields_2", Paccser_uint64_size10B_fields_2Message);
    GEN("ser10_fields_3", Paccser_uint64_size10B_fields_3Message);
    GEN("ser10_fields_4", Paccser_uint64_size10B_fields_4Message);
    GEN("ser10_fields_5", Paccser_uint64_size10B_fields_5Message);
    GEN("ser10_fields_6", Paccser_uint64_size10B_fields_6Message);
    GEN("ser10_fields_7", Paccser_uint64_size10B_fields_7Message);
    GEN("ser10_fields_8", Paccser_uint64_size10B_fields_8Message);
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
#else
int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    GEN("double", PaccdoubleMessage);
    GEN("double_repeated", Paccdouble_repeatedMessage);
    GEN("float", PaccfloatMessage);
    GEN("float_repeated", Paccfloat_repeatedMessage);
    GEN("int32", Paccint32Message);
    GEN("int32_repeated", Paccint32_repeatedMessage);
    GEN("int64", Paccint64Message);
    GEN("int64_repeated", Paccint64_repeatedMessage);
    GEN("uint32", Paccuint32Message);
    GEN("sint32", Paccsint32Message);
    GEN("sint64", Paccsint64Message);
    GEN("sint64_repeated", Paccsint64_repeatedMessage);
    GEN("fixed32", Paccfixed32Message);
    GEN("fixed64", Paccfixed64Message);
    GEN("sfixed32", Paccsfixed32Message);
    GEN("sfixed64", Paccsfixed64Message);
    GEN("bool", PaccboolMessage);
    GEN("bool_repeated", Paccbool_repeatedMessage);
    GEN("string", PaccstringMessage);
    GEN("string_15", Paccstring_15Message);
    GEN("string_long", Paccstring_longMessage);
    GEN("string_very_long", Paccstring_very_longMessage);
    GEN("bytes", PaccbytesMessage);
    GEN("bytes_15", Paccbytes_15Message);
    GEN("bytes_long", Paccbytes_longMessage);
    GEN("bytes_repeated", Paccbytes_repeatedMessage);
    GEN("bytes_very_long", Paccbytes_very_longMessage);
    GEN("nested_double", PaccPaccdoubleMessageMessage);
    GEN("nested_bool", PaccPaccboolMessageMessage);
    GEN("nested_string", PaccPaccstringMessageMessage);
    GEN("uint64_size00B", Paccuint64_size00BMessage);
    GEN("uint64_size01B", Paccuint64_size01BMessage);
    GEN("uint64_size02B", Paccuint64_size02BMessage);
    GEN("uint64_size03B", Paccuint64_size03BMessage);
    GEN("uint64_size04B", Paccuint64_size04BMessage);
    GEN("uint64_size05B", Paccuint64_size05BMessage);
    GEN("uint64_size06B", Paccuint64_size06BMessage);
    GEN("uint64_size07B", Paccuint64_size07BMessage);
    GEN("uint64_size08B", Paccuint64_size08BMessage);
    GEN("uint64_size09B", Paccuint64_size09BMessage);
    GEN("uint64_size10B", Paccuint64_size10BMessage);
    GEN("uint64_size00B_repeated", Paccuint64_size00B_repeatedMessage);
    GEN("uint64_size01B_repeated", Paccuint64_size01B_repeatedMessage);
    GEN("uint64_size02B_repeated", Paccuint64_size02B_repeatedMessage);
    GEN("uint64_size03B_repeated", Paccuint64_size03B_repeatedMessage);
    GEN("uint64_size04B_repeated", Paccuint64_size04B_repeatedMessage);
    GEN("uint64_size05B_repeated", Paccuint64_size05B_repeatedMessage);
    GEN("uint64_size06B_repeated", Paccuint64_size06B_repeatedMessage);
    GEN("uint64_size07B_repeated", Paccuint64_size07B_repeatedMessage);
    GEN("uint64_size08B_repeated", Paccuint64_size08B_repeatedMessage);
    GEN("uint64_size09B_repeated", Paccuint64_size09B_repeatedMessage);
    GEN("uint64_size10B_repeated", Paccuint64_size10B_repeatedMessage);
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
#endif
