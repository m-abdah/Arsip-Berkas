#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define DIR_SEPARATOR '\\'
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define DIR_SEPARATOR '/'
#endif

#define SIGNATURE "ABDAH"
#define SIGNATURE_LEN 5
#define VERSI "6.1.0"

// Flag enkripsi dan kompresi
#define FLAG_NORMAL 0
#define FLAG_ENKRIP 1
#define FLAG_KOMPRES 2
#define FLAG_ENKRIP_KOMPRES 3

// Buffer sizes
#define CHUNK_SIZE 65536        // 64KB chunk untuk I/O
#define STACK_BUFFER 65536      // 64KB stack buffer
#define IO_BUFFER (1024 * 1024) // 1MB I/O buffer

// ============================================================
//                    ZAST COMPRESSION ENGINE
// ============================================================
#define ZAST_LEVEL_NORMAL 1
#define ZAST_LEVEL_HIGH 2
#define ZAST_LEVEL_ULTRA 3

typedef struct {
  int level;
  size_t hash_size;
  size_t window_size;
} ZastConfig;

#define MIN_MATCH 4
#define MAX_MATCH (255 + MIN_MATCH)

ZastConfig zast_get_optimal_config() {
  ZastConfig config;
  config.level = ZAST_LEVEL_NORMAL;
  config.hash_size = 4096;
  config.window_size = 4095;

  unsigned long long total_ram_mb = 0;
#ifdef _WIN32
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
    total_ram_mb = status.ullTotalPhys / (1024 * 1024);
  }
#else
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && page_size > 0) {
    total_ram_mb = (unsigned long long)pages * page_size / (1024 * 1024);
  }
#endif

  if (total_ram_mb >= 8192) {
    // Ultra: >= 8GB RAM
    config.level = ZAST_LEVEL_ULTRA;
    config.hash_size = 65536;
    config.window_size = 65535;
  } else if (total_ram_mb >= 4096) {
    // High: >= 4GB RAM
    config.level = ZAST_LEVEL_HIGH;
    config.hash_size = 16384;
    config.window_size = 16383;
  } else {
    // Normal: < 4GB RAM
    config.level = ZAST_LEVEL_NORMAL;
    config.hash_size = 4096;
    config.window_size = 4095;
  }
  return config;
}

static inline uint32_t hash_3bytes(const uint8_t *p, size_t hash_size) {
  uint32_t h = (p[0] << 16) | (p[1] << 8) | p[2];
  h = (h * 506832829U) >> (32 - 16);
  return h & (hash_size - 1);
}

size_t zast_compress_chunk(const uint8_t *in, size_t in_len, uint8_t *out,
                           ZastConfig config) {
  if (in_len == 0)
    return 0;

  uint16_t *hash_table = (uint16_t *)calloc(config.hash_size, sizeof(uint16_t));
  if (!hash_table)
    return 0;

  size_t in_pos = 0;
  size_t out_pos = 0;

  while (in_pos < in_len) {
    size_t tag_pos = out_pos++;
    uint8_t tag = 0;
    int bit_idx;

    for (bit_idx = 0; bit_idx < 8 && in_pos < in_len; bit_idx++) {
      size_t match_len = 0;
      size_t match_dist = 0;

      if (in_pos + MIN_MATCH <= in_len) {
        uint32_t h = hash_3bytes(&in[in_pos], config.hash_size);
        size_t ref_pos = hash_table[h];
        hash_table[h] = (uint16_t)(in_pos & 0xFFFF);

        if (ref_pos > 0 && in_pos > ref_pos) {
          size_t dist = in_pos - ref_pos;
          if (dist <= config.window_size) {
            while (match_len < MAX_MATCH && in_pos + match_len < in_len &&
                   in[ref_pos + match_len] == in[in_pos + match_len]) {
              match_len++;
            }
            if (match_len >= MIN_MATCH) {
              match_dist = dist;
            } else {
              match_len = 0;
            }
          }
        }
      }

      if (match_len >= MIN_MATCH) {
        tag |= (1 << bit_idx);
        out[out_pos++] = (uint8_t)(match_dist >> 8);
        out[out_pos++] = (uint8_t)(match_dist & 0xFF);
        out[out_pos++] = (uint8_t)(match_len - MIN_MATCH);
        in_pos += match_len;
      } else {
        out[out_pos++] = in[in_pos++];
      }
    }
    out[tag_pos] = tag;
  }

  free(hash_table);

  if (out_pos >= in_len)
    return 0;
  return out_pos;
}

size_t zast_decompress_chunk(const uint8_t *in, size_t in_len, uint8_t *out,
                             size_t max_out_len) {
  if (in_len == 0)
    return 0;

  size_t in_pos = 0;
  size_t out_pos = 0;

  while (in_pos < in_len && out_pos < max_out_len) {
    uint8_t tag = in[in_pos++];

    for (int bit_idx = 0;
         bit_idx < 8 && in_pos < in_len && out_pos < max_out_len; bit_idx++) {
      if (tag & (1 << bit_idx)) {
        if (in_pos + 2 >= in_len)
          return 0;

        size_t dist = (in[in_pos] << 8) | in[in_pos + 1];
        in_pos += 2;
        size_t len = in[in_pos++] + MIN_MATCH;

        if (dist == 0 || dist > out_pos)
          return 0;
        if (out_pos + len > max_out_len)
          return 0;

        size_t ref_pos = out_pos - dist;
        for (size_t i = 0; i < len; i++) {
          out[out_pos++] = out[ref_pos++];
        }
      } else {
        out[out_pos++] = in[in_pos++];
      }
    }
  }
  return out_pos;
}

// ============================================================
//                    CRC32 (IEEE 802.3)
// ============================================================
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

void crc32_init() {
  if (crc32_initialized)
    return;
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t crc = i;
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    crc32_table[i] = crc;
  }
  crc32_initialized = 1;
}

uint32_t crc32_compute(const uint8_t *data, size_t len) {
  crc32_init();
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
  }
  return crc ^ 0xFFFFFFFF;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
  crc32_init();
  for (size_t i = 0; i < len; i++) {
    crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
  }
  return crc;
}

uint32_t crc32_finalize(uint32_t crc) { return crc ^ 0xFFFFFFFF; }

// ============================================================
//                    FORMAT UKURAN (B, KB, MB, GB, TB)
// ============================================================
void format_ukuran(size_t bytes, char *output, size_t output_size) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double size = (double)bytes;

  while (size >= 1024.0 && unit_index < 4) {
    size /= 1024.0;
    unit_index++;
  }

  if (unit_index == 0)
    snprintf(output, output_size, "%.0f %s", size, units[unit_index]);
  else if (size < 10.0)
    snprintf(output, output_size, "%.2f %s", size, units[unit_index]);
  else if (size < 100.0)
    snprintf(output, output_size, "%.1f %s", size, units[unit_index]);
  else
    snprintf(output, output_size, "%.0f %s", size, units[unit_index]);
}

// ============================================================
//                    XOR ENKRIPSI / DEKRIPSI
// ============================================================
void xor_cipher(uint8_t *data, size_t len, const char *key, size_t key_len,
                size_t offset) {
  for (size_t i = 0; i < len; i++) {
    data[i] ^= key[(offset + i) % key_len];
  }
}

// ============================================================
//                    FUNGSI BANTU: STRING
// ============================================================
char *my_strdup(const char *s) {
  if (s == NULL)
    return NULL;
  size_t len = strlen(s) + 1;
  char *copy = (char *)malloc(len);
  if (copy)
    memcpy(copy, s, len);
  return copy;
}

char *gabung_path(const char *folder, const char *file) {
  size_t len_folder = strlen(folder);
  size_t len_file = strlen(file);
  size_t perlu_sep =
      (len_folder > 0 && folder[len_folder - 1] != DIR_SEPARATOR) ? 1 : 0;
  size_t total = len_folder + len_file + perlu_sep + 1;
  char *hasil = (char *)malloc(total);
  if (hasil == NULL)
    return NULL;
  strcpy(hasil, folder);
  if (perlu_sep) {
    hasil[len_folder] = DIR_SEPARATOR;
    hasil[len_folder + 1] = '\0';
  }
  strcat(hasil, file);
  return hasil;
}

int is_directory(const char *path) {
#ifdef _WIN32
  DWORD attr = GetFileAttributes(path);
  if (attr == INVALID_FILE_ATTRIBUTES)
    return 0;
  return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  return S_ISDIR(st.st_mode);
#endif
}

// ============================================================
//                    TULIS NAMA (DINAMIS)
// ============================================================
void tulis_nama(FILE *fp, const char *nama, const char *key, size_t key_len,
                int enkrip) {
  uint16_t panjang = (uint16_t)strlen(nama);
  if (enkrip && key_len > 0) {
    char *nama_enc = my_strdup(nama);
    xor_cipher((uint8_t *)nama_enc, panjang, key, key_len, 0);
    fwrite(&panjang, sizeof(uint16_t), 1, fp);
    fwrite(nama_enc, 1, panjang, fp);
    free(nama_enc);
  } else {
    fwrite(&panjang, sizeof(uint16_t), 1, fp);
    fwrite(nama, 1, panjang, fp);
  }
}

// ============================================================
//                    BACA NAMA (DINAMIS)
// ============================================================
char *baca_nama(FILE *fp, const char *key, size_t key_len, int enkrip) {
  uint16_t panjang;
  if (fread(&panjang, sizeof(uint16_t), 1, fp) != 1)
    return NULL;
  char *nama = (char *)malloc(panjang + 1);
  if (nama == NULL)
    return NULL;
  fread(nama, 1, panjang, fp);
  nama[panjang] = '\0';
  if (enkrip && key_len > 0) {
    xor_cipher((uint8_t *)nama, panjang, key, key_len, 0);
  }
  return nama;
}

// ============================================================
//                    VALIDASI SIGNATURE
// ============================================================
int validasi_signature(FILE *fp) {
  char sig[SIGNATURE_LEN];
  fread(sig, 1, SIGNATURE_LEN, fp);
  if (memcmp(sig, SIGNATURE, SIGNATURE_LEN) != 0) {
    fprintf(stderr, "Bukan container ABDAH!\n");
    return 0;
  }
  return 1;
}

// ============================================================
//                    TULIS SATU BERKAS (OPTIMIZED STREAMING)
// ============================================================
int tulis_satu_berkas(FILE *keluar, const char *path_asli,
                      const char *nama_dalam, const char *key, size_t key_len,
                      uint8_t flag) {
  int enkrip = (flag == FLAG_ENKRIP || flag == FLAG_ENKRIP_KOMPRES);
  int kompres = (flag == FLAG_KOMPRES || flag == FLAG_ENKRIP_KOMPRES);

  FILE *masuk = fopen(path_asli, "rb");
  if (masuk == NULL) {
    fprintf(stderr, "Gagal buka: %s\n", path_asli);
    return 0;
  }

  setvbuf(masuk, NULL, _IOFBF, IO_BUFFER);
  fseek(masuk, 0, SEEK_END);
  size_t ukuran = (size_t)ftell(masuk);
  rewind(masuk);

  tulis_nama(keluar, nama_dalam, key, key_len, enkrip);
  fwrite(&ukuran, sizeof(size_t), 1, keluar);

  uint8_t chunk[CHUNK_SIZE];
  size_t total_read = 0;
  size_t total_written_data = 0;

  if (kompres) {
    ZastConfig zc = zast_get_optimal_config();
    uint8_t comp_chunk[CHUNK_SIZE * 2];

    while (total_read < ukuran) {
      size_t to_read = (ukuran - total_read < CHUNK_SIZE)
                           ? (ukuran - total_read)
                           : CHUNK_SIZE;
      size_t read = fread(chunk, 1, to_read, masuk);
      if (read == 0)
        break;

      uint32_t c_size =
          (uint32_t)zast_compress_chunk(chunk, read, comp_chunk, zc);
      uint32_t header[2];

      if (c_size > 0) {
        if (enkrip && key_len > 0)
          xor_cipher(comp_chunk, c_size, key, key_len, total_written_data);
        header[0] = c_size;
        header[1] = (uint32_t)read;
        fwrite(header, sizeof(uint32_t), 2, keluar);
        fwrite(comp_chunk, 1, c_size, keluar);
        total_written_data += c_size;
      } else {
        if (enkrip && key_len > 0)
          xor_cipher(chunk, read, key, key_len, total_written_data);
        header[0] = 0;
        header[1] = (uint32_t)read;
        fwrite(header, sizeof(uint32_t), 2, keluar);
        fwrite(chunk, 1, read, keluar);
        total_written_data += read;
      }
      total_read += read;
    }
  } else {
    while (total_read < ukuran) {
      size_t to_read = (ukuran - total_read < CHUNK_SIZE)
                           ? (ukuran - total_read)
                           : CHUNK_SIZE;
      size_t read = fread(chunk, 1, to_read, masuk);
      if (read == 0)
        break;

      if (enkrip && key_len > 0)
        xor_cipher(chunk, read, key, key_len, total_written_data);
      fwrite(chunk, 1, read, keluar);
      total_written_data += read;
      total_read += read;
    }
  }

  fclose(masuk);
  char ukuran_str[32];
  format_ukuran(ukuran, ukuran_str, sizeof(ukuran_str));
  fprintf(stderr, "%s (%s)\n", nama_dalam, ukuran_str);
  return 1;
}

// ============================================================
//                    SCAN DIREKTORI (DINAMIS)
// ============================================================
#ifndef _WIN32
size_t scan_direktori(const char *folder, const char *prefix,
                      char ***daftar_path, char ***daftar_nama,
                      size_t *kapasitas, size_t *jumlah) {
  DIR *dir = opendir(folder);
  if (dir == NULL) {
    fprintf(stderr, "Gagal buka direktori: %s\n", folder);
    return 0;
  }

  struct dirent *entry;
  size_t ditambahkan = 0;

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char *path_asli = gabung_path(folder, entry->d_name);
    char *nama_dalam = (prefix[0] == '\0') ? my_strdup(entry->d_name)
                                           : gabung_path(prefix, entry->d_name);

    if (is_directory(path_asli)) {
      ditambahkan += scan_direktori(path_asli, nama_dalam, daftar_path,
                                    daftar_nama, kapasitas, jumlah);
      free(path_asli);
      free(nama_dalam);
    } else {
      if (*jumlah >= *kapasitas) {
        *kapasitas *= 2;
        *daftar_path =
            (char **)realloc(*daftar_path, *kapasitas * sizeof(char *));
        *daftar_nama =
            (char **)realloc(*daftar_nama, *kapasitas * sizeof(char *));
      }
      (*daftar_path)[*jumlah] = path_asli;
      (*daftar_nama)[*jumlah] = nama_dalam;
      (*jumlah)++;
      ditambahkan++;
    }
  }
  closedir(dir);
  return ditambahkan;
}
#else
size_t scan_direktori(const char *folder, const char *prefix,
                      char ***daftar_path, char ***daftar_nama,
                      size_t *kapasitas, size_t *jumlah) {
  char *pattern = gabung_path(folder, "*");
  WIN32_FIND_DATA findData;
  HANDLE hFind = FindFirstFile(pattern, &findData);
  free(pattern);

  if (hFind == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "Gagal buka direktori: %s\n", folder);
    return 0;
  }

  size_t ditambahkan = 0;
  do {
    if (strcmp(findData.cFileName, ".") == 0 ||
        strcmp(findData.cFileName, "..") == 0)
      continue;

    char *path_asli = gabung_path(folder, findData.cFileName);
    char *nama_dalam = (prefix[0] == '\0')
                           ? my_strdup(findData.cFileName)
                           : gabung_path(prefix, findData.cFileName);

    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      ditambahkan += scan_direktori(path_asli, nama_dalam, daftar_path,
                                    daftar_nama, kapasitas, jumlah);
      free(path_asli);
      free(nama_dalam);
    } else {
      if (*jumlah >= *kapasitas) {
        *kapasitas *= 2;
        *daftar_path =
            (char **)realloc(*daftar_path, *kapasitas * sizeof(char *));
        *daftar_nama =
            (char **)realloc(*daftar_nama, *kapasitas * sizeof(char *));
      }
      (*daftar_path)[*jumlah] = path_asli;
      (*daftar_nama)[*jumlah] = nama_dalam;
      (*jumlah)++;
      ditambahkan++;
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);
  return ditambahkan;
}
#endif

// ============================================================
//                    PROSES INPUT & TULIS
// ============================================================
int proses_input_dan_tulis(FILE *keluar, char *file_list[], size_t jml_file,
                           char *dir_list[], size_t jml_dir, const char *key) {
  size_t key_len = key ? strlen(key) : 0;
  int enkrip = (key_len > 0) ? 1 : 0;

  uint8_t flag = enkrip ? FLAG_ENKRIP_KOMPRES : FLAG_KOMPRES;

  size_t kapasitas = 256;
  size_t total_file = 0;
  char **daftar_path = (char **)malloc(kapasitas * sizeof(char *));
  char **daftar_nama = (char **)malloc(kapasitas * sizeof(char *));

  for (size_t i = 0; i < jml_file; i++) {
    if (is_directory(file_list[i])) {
      fprintf(stderr, "\"%s\" adalah direktori, gunakan -d\n", file_list[i]);
      continue;
    }
    if (total_file >= kapasitas) {
      kapasitas *= 2;
      daftar_path = (char **)realloc(daftar_path, kapasitas * sizeof(char *));
      daftar_nama = (char **)realloc(daftar_nama, kapasitas * sizeof(char *));
    }
    daftar_path[total_file] = my_strdup(file_list[i]);
    const char *nama_saja = strrchr(file_list[i], DIR_SEPARATOR);
    daftar_nama[total_file] =
        nama_saja ? my_strdup(nama_saja + 1) : my_strdup(file_list[i]);
    total_file++;
  }

  for (size_t i = 0; i < jml_dir; i++) {
    if (!is_directory(dir_list[i])) {
      fprintf(stderr, "\"%s\" bukan direktori, gunakan -f\n", dir_list[i]);
      continue;
    }
    fprintf(stderr, "Memindai direktori: %s\n", dir_list[i]);
    const char *nama_folder = strrchr(dir_list[i], DIR_SEPARATOR);
    nama_folder = nama_folder ? nama_folder + 1 : dir_list[i];
    size_t ditambah = scan_direktori(dir_list[i], nama_folder, &daftar_path,
                                     &daftar_nama, &kapasitas, &total_file);
    fprintf(stderr, " %lu file ditemukan\n\n", (unsigned long)ditambah);
  }

  if (total_file == 0) {
    fprintf(stderr, "Tidak ada file untuk diarsipkan\n");
    free(daftar_path);
    free(daftar_nama);
    return 0;
  }

  fprintf(stderr, "Menulis %lu file ke container...\n",
          (unsigned long)total_file);
  fprintf(stderr, "Kompresi ZAST AKTIF (Otomatis menyesuaikan RAM)\n");
  if (enkrip)
    fprintf(stderr, "Enkripsi XOR AKTIF (password: %lu karakter)\n\n",
            (unsigned long)key_len);

  setvbuf(keluar, NULL, _IOFBF, IO_BUFFER);

  fwrite(SIGNATURE, 1, SIGNATURE_LEN, keluar);
  fwrite(&flag, sizeof(uint8_t), 1, keluar);
  fwrite(&total_file, sizeof(size_t), 1, keluar);

  for (size_t i = 0; i < total_file; i++) {
    tulis_satu_berkas(keluar, daftar_path[i], daftar_nama[i], key, key_len,
                      flag);
    free(daftar_path[i]);
    free(daftar_nama[i]);
  }

  free(daftar_path);
  free(daftar_nama);
  return 1;
}

// ============================================================
//                    MINTA PASSWORD
// ============================================================
char *minta_password(const char *prompt) {
  fprintf(stderr, "%s", prompt);
  size_t kap = 64;
  size_t len = 0;
  char *pass = (char *)malloc(kap);
  if (pass == NULL)
    return NULL;

  int c;
  while ((c = getchar()) != '\n' && c != EOF) {
    if (len + 1 >= kap) {
      kap *= 2;
      pass = (char *)realloc(pass, kap);
      if (pass == NULL)
        return NULL;
    }
    pass[len++] = (char)c;
  }
  pass[len] = '\0';

  if (len == 0 && c == EOF) {
    free(pass);
    return NULL;
  }
  return pass;
}

// ============================================================
//                    BACA / LIST ISI (DENGAN CRC32)
// ============================================================
int baca_multi_berkas(const char *nama_container, const char *key) {
  FILE *masuk = fopen(nama_container, "rb");
  if (masuk == NULL) {
    fprintf(stderr, "Tidak bisa membuka: %s\n", nama_container);
    return 0;
  }

  setvbuf(masuk, NULL, _IOFBF, IO_BUFFER);

  if (!validasi_signature(masuk)) {
    fclose(masuk);
    return 0;
  }

  uint8_t flag;
  fread(&flag, sizeof(uint8_t), 1, masuk);
  size_t key_len = key ? strlen(key) : 0;

  int enkrip = (flag == FLAG_ENKRIP || flag == FLAG_ENKRIP_KOMPRES);
  int kompres = (flag == FLAG_KOMPRES || flag == FLAG_ENKRIP_KOMPRES);

  if (enkrip && key_len == 0) {
    fprintf(stderr, "Container ini TERENKRIPSI!\n");
    fprintf(stderr, "   Gunakan: abdah -p \"password\" -l %s\n\n",
            nama_container);
    fclose(masuk);
    return 0;
  }

  if (enkrip && kompres)
    printf("Container terenkripsi & terkompres (XOR + Zast)\n\n");
  else if (enkrip)
    printf("Container terenkripsi (XOR)\n\n");
  else if (kompres)
    printf("Container terkompres (Zast)\n\n");
  else
    printf("Container normal (tanpa enkripsi)\n\n");

  size_t jumlah;
  fread(&jumlah, sizeof(size_t), 1, masuk);

  size_t total_ukuran = 0;
  size_t file_count = 0;
  size_t depth_max = 0;
  int error_crc = 0;

  printf("Container \"%s\" - %lu entri:\n\n", nama_container,
         (unsigned long)jumlah);
  printf("%-6s %-42s %12s  %-10s %s\n", "No", "Nama", "Ukuran", "CRC32",
         "Status");
  printf("---------------------------------------------------------------------"
         "-------------------\n");

  for (size_t i = 0; i < jumlah; i++) {
    char *nama = baca_nama(masuk, key, key_len, enkrip);
    if (nama == NULL)
      break;

    size_t ukuran;
    fread(&ukuran, sizeof(size_t), 1, masuk);

    uint32_t crc = 0xFFFFFFFF;

    if (kompres) {
      size_t total_extracted = 0;
      size_t total_read_data = 0;
      uint8_t comp_chunk[CHUNK_SIZE * 2];
      uint8_t orig_chunk[CHUNK_SIZE];

      while (total_extracted < ukuran) {
        uint32_t header[2];
        if (fread(header, sizeof(uint32_t), 2, masuk) != 2)
          break;
        uint32_t c_size = header[0];
        uint32_t o_size = header[1];

        if (c_size > 0) {
          fread(comp_chunk, 1, c_size, masuk);
          if (enkrip)
            xor_cipher(comp_chunk, c_size, key, key_len, total_read_data);
          size_t d_size =
              zast_decompress_chunk(comp_chunk, c_size, orig_chunk, o_size);
          if (d_size == o_size) {
            crc = crc32_update(crc, orig_chunk, o_size);
          } else {
            error_crc++;
          }
          total_read_data += c_size;
          total_extracted += o_size;
        } else {
          fread(orig_chunk, 1, o_size, masuk);
          if (enkrip)
            xor_cipher(orig_chunk, o_size, key, key_len, total_read_data);
          crc = crc32_update(crc, orig_chunk, o_size);
          total_read_data += o_size;
          total_extracted += o_size;
        }
      }
    } else {
      uint8_t chunk[CHUNK_SIZE];
      size_t total_read = 0;
      while (total_read < ukuran) {
        size_t to_read = (ukuran - total_read < CHUNK_SIZE)
                             ? (ukuran - total_read)
                             : CHUNK_SIZE;
        size_t read = fread(chunk, 1, to_read, masuk);
        if (read == 0)
          break;

        if (enkrip && key_len > 0) {
          xor_cipher(chunk, read, key, key_len, total_read);
        }
        crc = crc32_update(crc, chunk, read);
        total_read += read;
      }
    }

    crc = crc32_finalize(crc);

    char ukuran_str[32];
    format_ukuran(ukuran, ukuran_str, sizeof(ukuran_str));

    const char *ikon = "";
    if (strchr(nama, DIR_SEPARATOR)) {
      size_t depth = 0;
      for (char *p = nama; *p; p++)
        if (*p == DIR_SEPARATOR)
          depth++;
      if (depth > depth_max)
        depth_max = depth;
    }

    printf("%-6lu %s %-40s %12s  0x%08X  %s\n", (unsigned long)(i + 1), ikon,
           nama, ukuran_str, crc, crc != 0 ? "OK" : "EMPTY");

    total_ukuran += ukuran;
    file_count++;
    free(nama);
  }

  char total_str[32];
  format_ukuran(total_ukuran, total_str, sizeof(total_str));
  printf("---------------------------------------------------------------------"
         "-------------------\n");
  printf("Total: %lu file", (unsigned long)file_count);
  if (depth_max > 0)
    printf(", %lu level subdirektori", (unsigned long)depth_max);
  printf(" | %s (%lu bytes)\n", total_str, (unsigned long)total_ukuran);

  if (error_crc > 0) {
    printf("%d blok KORUP! Password salah atau data rusak.\n", error_crc);
  }

  fclose(masuk);
  return 1;
}

// ============================================================
//                    BUAT DIREKTORI
// ============================================================
int buat_direktori_untuk_file(const char *path_file) {
  char *path_salin = my_strdup(path_file);
  if (path_salin == NULL)
    return 0;
  char *sep = strrchr(path_salin, DIR_SEPARATOR);
  if (sep == NULL) {
    free(path_salin);
    return 1;
  }
  *sep = '\0';

#ifdef _WIN32
  char *p = path_salin;
  while ((p = strchr(p, DIR_SEPARATOR)) != NULL) {
    *p = '\0';
    CreateDirectory(path_salin, NULL);
    *p = DIR_SEPARATOR;
    p++;
  }
  CreateDirectory(path_salin, NULL);
#else
  size_t len = strlen(path_salin) + 10;
  char *cmd = (char *)malloc(len);
  if (cmd) {
    snprintf(cmd, len, "mkdir -p \"%s\"", path_salin);
    system(cmd);
    free(cmd);
  }
#endif

  free(path_salin);
  return 1;
}

// ============================================================
//                    EKSTRAK (OPTIMIZED STREAMING)
// ============================================================
int ekstrak_semua(const char *nama_container, const char *key,
                  const char *target_dir) {
  FILE *masuk = fopen(nama_container, "rb");
  if (masuk == NULL) {
    fprintf(stderr, "Tidak bisa membuka: %s\n", nama_container);
    return 0;
  }

  setvbuf(masuk, NULL, _IOFBF, IO_BUFFER);

  if (!validasi_signature(masuk)) {
    fclose(masuk);
    return 0;
  }

  uint8_t flag;
  fread(&flag, sizeof(uint8_t), 1, masuk);
  size_t key_len = key ? strlen(key) : 0;

  int enkrip = (flag == FLAG_ENKRIP || flag == FLAG_ENKRIP_KOMPRES);
  int kompres = (flag == FLAG_KOMPRES || flag == FLAG_ENKRIP_KOMPRES);

  if (enkrip && key_len == 0) {
    fprintf(stderr, "Container TERENKRIPSI! Gunakan -p \"password\"\n");
    fclose(masuk);
    return 0;
  }

  size_t jumlah;
  fread(&jumlah, sizeof(size_t), 1, masuk);
  fprintf(stderr, "Mengekstrak %lu entri...\n\n", (unsigned long)jumlah);

  for (size_t i = 0; i < jumlah; i++) {
    char *nama = baca_nama(masuk, key, key_len, enkrip);
    if (nama == NULL)
      break;

    char *path_ekstrak = nama;
    if (target_dir != NULL && target_dir[0] != '\0') {
      path_ekstrak = gabung_path(target_dir, nama);
    }

    size_t ukuran;
    fread(&ukuran, sizeof(size_t), 1, masuk);
    buat_direktori_untuk_file(path_ekstrak);

    FILE *keluar = fopen(path_ekstrak, "wb");
    if (keluar == NULL) {
      fprintf(stderr, "Gagal membuat: %s\n", path_ekstrak);

      // Skip data
      if (kompres) {
        size_t total_extracted = 0;
        while (total_extracted < ukuran) {
          uint32_t header[2];
          if (fread(header, sizeof(uint32_t), 2, masuk) != 2)
            break;
          if (header[0] > 0)
            fseek(masuk, header[0], SEEK_CUR);
          else
            fseek(masuk, header[1], SEEK_CUR);
          total_extracted += header[1];
        }
      } else {
        fseek(masuk, (long)ukuran, SEEK_CUR);
      }
      if (path_ekstrak != nama)
        free(path_ekstrak);
      free(nama);
      continue;
    }

    setvbuf(keluar, NULL, _IOFBF, IO_BUFFER);

    if (kompres) {
      size_t total_extracted = 0;
      size_t total_read_data = 0;
      uint8_t comp_chunk[CHUNK_SIZE * 2];
      uint8_t orig_chunk[CHUNK_SIZE];

      while (total_extracted < ukuran) {
        uint32_t header[2];
        if (fread(header, sizeof(uint32_t), 2, masuk) != 2)
          break;

        uint32_t c_size = header[0];
        uint32_t o_size = header[1];

        if (c_size > 0) {
          fread(comp_chunk, 1, c_size, masuk);
          if (enkrip)
            xor_cipher(comp_chunk, c_size, key, key_len, total_read_data);
          zast_decompress_chunk(comp_chunk, c_size, orig_chunk, o_size);
          fwrite(orig_chunk, 1, o_size, keluar);
          total_read_data += c_size;
          total_extracted += o_size;
        } else {
          fread(orig_chunk, 1, o_size, masuk);
          if (enkrip)
            xor_cipher(orig_chunk, o_size, key, key_len, total_read_data);
          fwrite(orig_chunk, 1, o_size, keluar);
          total_read_data += o_size;
          total_extracted += o_size;
        }
      }
    } else {
      uint8_t chunk[CHUNK_SIZE];
      size_t total_read = 0;
      while (total_read < ukuran) {
        size_t to_read = (ukuran - total_read < CHUNK_SIZE)
                             ? (ukuran - total_read)
                             : CHUNK_SIZE;
        size_t read = fread(chunk, 1, to_read, masuk);
        if (read == 0)
          break;

        if (enkrip && key_len > 0) {
          xor_cipher(chunk, read, key, key_len, total_read);
        }
        fwrite(chunk, 1, read, keluar);
        total_read += read;
      }
    }

    fclose(keluar);

    char ukuran_str[32];
    format_ukuran(ukuran, ukuran_str, sizeof(ukuran_str));
    fprintf(stderr, "%s (%s)\n", path_ekstrak, ukuran_str);
    if (path_ekstrak != nama)
      free(path_ekstrak);
    free(nama);
  }

  fclose(masuk);
  fprintf(stderr, "\nEkstrak selesai!\n");
  return 1;
}

// ============================================================
//                    HELP
// ============================================================
void tampilkan_help(const char *program) {
  printf("ABDAH Archiver v%s (Optimized Streaming + Auto-size Format)\n\n",
         VERSI);
  printf("Pemakaian:\n\n");
  printf("  Arsipkan (Otomatis dengan Zast Compression):\n");
  printf("    %s -f <file...> -o <output>\n", program);
  printf("    %s -d <direktori> -o arsip.dat\n\n", program);
  printf("  Arsipkan dengan enkripsi:\n");
  printf("    %s -p \"password\" -f <file...> -o <output>\n", program);
  printf("    %s -p \"rahasia\" -d <direktori> -o arsip.dat\n\n", program);
  printf("  Ekstrak:\n");
  printf("    %s -x <container>\n", program);
  printf("    %s -p \"password\" -x <container>\n", program);
  printf("    %s -x <container> -ke <dir>\n\n", program);
  printf("Opsi:\n");
  printf(
      "  -p <password>   Password enkripsi XOR (dilakukan setelah kompresi)\n");
  printf("  -f <file...>    Daftar file\n");
  printf("  -d <dir>        Direktori (rekursif)\n");
  printf("  -o <output>     File output\n");
  printf("  -l <container>  List isi (dengan CRC32)\n");
  printf("  -x <container>  Ekstrak\n");
  printf("  -ke <dir>       Ekstrak ke direktori tujuan\n");
  printf("  --help          Bantuan\n");
  printf("  --version       Versi\n");
  printf("\nFitur Optimasi:\n");
  printf("   Streaming Zast Compression adaptif sesuai RAM (Otomatis)\n");
  printf("   Streaming I/O dengan buffer 1MB\n");
  printf("   CRC32 dihitung saat -l saja\n");
}

void tampilkan_versi() {
  printf("ABDAH Archiver v%s\n", VERSI);
  printf("Fitur: Zast Compression (Otomatis) + XOR Encryption + CRC32\n");
  printf(
      "Optimasi: Adaptive Compression Level, Streaming I/O, Large buffers\n");
}

// ============================================================
//                    MAIN
// ============================================================
int main(int argc, char *argv[]) {
  crc32_init();

  if (argc < 2) {
    tampilkan_help(argv[0]);
    return 1;
  }

  char *file_list[1024];
  size_t jml_file = 0;
  char *dir_list[1024];
  size_t jml_dir = 0;
  char *file_output = NULL;
  char *file_container = NULL;
  char *password = NULL;
  char *target_dir = NULL;
  int mode = 0; // 1=create, 2=list, 3=extract
  int flag_f = 0, flag_d = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      tampilkan_help(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--version") == 0) {
      tampilkan_versi();
      return 0;
    } else if (strcmp(argv[i], "-p") == 0) {
      flag_f = 0;
      flag_d = 0;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        password = argv[++i];
      } else {
        fprintf(stderr, "-p butuh password\n");
        return 1;
      }
    } else if (strcmp(argv[i], "-f") == 0) {
      mode = 1;
      flag_f = 1;
      flag_d = 0;
    } else if (strcmp(argv[i], "-d") == 0) {
      mode = 1;
      flag_d = 1;
      flag_f = 0;
    } else if (strcmp(argv[i], "-o") == 0) {
      flag_f = 0;
      flag_d = 0;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        file_output = argv[++i];
      } else {
        fprintf(stderr, "-o butuh nama output\n");
        return 1;
      }
    } else if (strcmp(argv[i], "-l") == 0) {
      mode = 2;
      flag_f = 0;
      flag_d = 0;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        file_container = argv[++i];
      } else {
        fprintf(stderr, "-l butuh container\n");
        return 1;
      }
    } else if (strcmp(argv[i], "-x") == 0) {
      mode = 3;
      flag_f = 0;
      flag_d = 0;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        file_container = argv[++i];
      } else {
        fprintf(stderr, "-x butuh container\n");
        return 1;
      }
    } else if (strcmp(argv[i], "-ke") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        target_dir = argv[++i];
      } else {
        fprintf(stderr, "-ke butuh direktori tujuan\n");
        return 1;
      }
    } else if (flag_f) {
      if (jml_file < 1024)
        file_list[jml_file++] = argv[i];
    } else if (flag_d) {
      if (jml_dir < 1024)
        dir_list[jml_dir++] = argv[i];
    } else {
      fprintf(stderr, "Argumen tidak dikenal: %s\n", argv[i]);
      return 1;
    }
  }

  if (mode == 1) {
    if (jml_file == 0 && jml_dir == 0) {
      fprintf(stderr, "Gunakan -f atau -d\n");
      return 1;
    }
    FILE *keluar = file_output ? fopen(file_output, "wb") : stdout;
    if (keluar == NULL) {
      fprintf(stderr, "Gagal membuat: %s\n", file_output);
      return 1;
    }
    if (keluar != stdout) {
      setvbuf(keluar, NULL, _IOFBF, IO_BUFFER);
    }

    int hasil = proses_input_dan_tulis(keluar, file_list, jml_file, dir_list,
                                       jml_dir, password);
    if (keluar != stdout)
      fclose(keluar);
    if (hasil) {
      fprintf(stderr, "\nContainer berhasil dibuat");
      if (file_output)
        fprintf(stderr, ": %s\n", file_output);
      else
        fprintf(stderr, " (stdout)\n");
    }
    return hasil ? 0 : 1;
  } else if (mode == 2) {
    if (file_container == NULL) {
      fprintf(stderr, "Gunakan: -l <container>\n");
      return 1;
    }
    return baca_multi_berkas(file_container, password) ? 0 : 1;
  } else if (mode == 3) {
    if (file_container == NULL) {
      fprintf(stderr, "Gunakan: -x <container>\n");
      return 1;
    }
    return ekstrak_semua(file_container, password, target_dir) ? 0 : 1;
  } else {
    fprintf(stderr, "Tidak ada aksi. Coba --help\n");
    return 1;
  }

  return 0;
}