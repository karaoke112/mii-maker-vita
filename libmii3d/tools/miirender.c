/* miirender - render a Mii head to an image on a PC.
 *
 *   miirender FFLResHigh.dat out.ppm [mii.charinfo|.ffsd|.nfsd|default|female] [size] [yaw]
 *   miirender FFLResHigh.dat --face out.ppm [mii] [size]     (face texture only)
 *   miirender FFLResHigh.dat --body FFLBodyRes.dat out.ppm [mii] [size] [yaw]
 *                                                            (whole Mii)
 *
 * Output is binary PPM (P6) on a light background, or PAM (P7, RGBA) when
 * the name ends in .pam.
 */
#include <mii/mii.h>
#include <mii3d/mii3d.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? (uint8_t *)malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static int load_mii(const char *arg, mii_charinfo_t *ci)
{
    if (!arg || !strcmp(arg, "default")) {
        mii_charinfo_init_default(ci, 0);
        return 0;
    }
    if (!strcmp(arg, "female")) {
        mii_charinfo_init_default(ci, 1);
        return 0;
    }
    size_t n;
    uint8_t *buf = read_all(arg, &n);
    if (!buf)
        return -1;
    mii_result_t r;
    if (n == MII_CHARINFO_SIZE) r = mii_charinfo_decode(ci, buf, n);
    else if (n == MII_COREDATA_SIZE) r = mii_coredata_decode(ci, buf, n);
    else if (n == MII_STOREDATA_SIZE) r = mii_storedata_decode(ci, buf, n, NULL);
    else r = mii_ver3_decode(ci, NULL, buf, n, NULL);
    free(buf);
    if (r == MII_ERR_INVALID)
        fprintf(stderr, "warning: %s has out-of-range fields\n", arg);
    return (r == MII_OK || r == MII_ERR_INVALID) ? 0 : -1;
}

static int write_image(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    const size_t len = strlen(path);
    if (len > 4 && !strcmp(path + len - 4, ".pam")) {
        fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", w, h);
        fwrite(rgba, 4, (size_t)w * h, f);
    } else {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) {
            const uint8_t *p = rgba + 4 * i;
            const int a = p[3];
            for (int c = 0; c < 3; c++)
                fputc((p[c] * a + 236 * (255 - a)) / 255, f); /* over light grey */
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s FFLResHigh.dat out.ppm [mii] [size] [yaw]\n"
                        "       %s FFLResHigh.dat --face out.ppm [mii] [size]\n"
                        "       %s FFLResHigh.dat --body FFLBodyRes.dat out.ppm [mii] [size] [yaw]\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }
    const int face_only = !strcmp(argv[2], "--face");
    const char *body_path = !strcmp(argv[2], "--body") && argc > 3 ? argv[3] : NULL;
    const int a = face_only ? 3 : body_path ? 4 : 2;
    if (argc <= a) {
        fprintf(stderr, "missing output path\n");
        return 2;
    }
    size_t size;
    uint8_t *file = read_all(argv[1], &size);
    if (!file) {
        perror(argv[1]);
        return 1;
    }
    mii3d_res_t *res;
    mii3d_result_t r = mii3d_res_open(&res, file, size);
    if (r != MII3D_OK) {
        fprintf(stderr, "%s: %s\n", argv[1], mii3d_result_string(r));
        return 1;
    }
    mii_charinfo_t ci;
    if (load_mii(argc > a + 1 ? argv[a + 1] : NULL, &ci) != 0) {
        fprintf(stderr, "could not read Mii %s\n", argv[a + 1]);
        return 1;
    }
    const int px = argc > a + 2 ? atoi(argv[a + 2]) : 256;
    mii3d_renderer_t *ren;
    mii3d_renderer_create(&ren, res);
    uint8_t *body_file = NULL;
    mii3d_res_t *body = NULL;
    if (body_path) {
        size_t body_size;
        body_file = read_all(body_path, &body_size);
        if (!body_file || (r = mii3d_res_open_body(&body, body_file, body_size)) != MII3D_OK) {
            fprintf(stderr, "%s: %s\n", body_path, body_file ? mii3d_result_string(r) : "cannot read");
            return 1;
        }
        mii3d_renderer_set_body(ren, body);
    }
    uint8_t *rgba = (uint8_t *)malloc((size_t)px * px * 4);
    if (face_only) {
        r = mii3d_render_face_texture(ren, &ci, rgba, px);
    } else {
        mii3d_view_t view;
        mii3d_view_init_default(&view);
        if (argc > a + 3)
            view.yaw_deg = (float)atof(argv[a + 3]);
        view.full_body = body != NULL;
        view.supersample = 2;
        r = mii3d_render_head(ren, &ci, &view, rgba, px, px);
    }
    if (r != MII3D_OK) {
        fprintf(stderr, "render: %s\n", mii3d_result_string(r));
        return 1;
    }
    write_image(argv[a], rgba, px, px);
    free(rgba);
    mii3d_renderer_destroy(ren);
    mii3d_res_close(body);
    free(body_file);
    mii3d_res_close(res);
    free(file);
    return 0;
}
