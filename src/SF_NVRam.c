/* File : SF_NVRam.c - persistent storage.
 * The 3DO saved to battery-backed NVRAM; this port stores files under
 * $SF_SAVEDIR, else (Windows) %APPDATA%/starfighter, else $XDG_DATA_HOME/starfighter,
 * else ~/.local/share/starfighter.
 * Data is stored exactly as the in-memory structure (native layout), so save files
 * are not interchangeable with 3DO saves. */
#include "SF_NVRam.h"
#include "SF_Message.h"
#include <sys/stat.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define SF_MKDIR(p) _mkdir(p)
#else
#define SF_MKDIR(p) mkdir((p), 0755)
#endif

static const char *savedir(void)
{
	static char dir[512];
	const char *e;
	if (dir[0]) return dir;
	if ((e = getenv("SF_SAVEDIR"))) snprintf(dir, sizeof dir, "%s", e);
	#ifdef _WIN32
	else if ((e = getenv("APPDATA"))) snprintf(dir, sizeof dir, "%s/starfighter", e);
#endif
	else if ((e = getenv("XDG_DATA_HOME"))) snprintf(dir, sizeof dir, "%s/starfighter", e);
	else if ((e = getenv("HOME"))) snprintf(dir, sizeof dir, "%s/.local/share/starfighter", e);
	else snprintf(dir, sizeof dir, ".starfighter");
	{ char tmp[512]; size_t i; snprintf(tmp, sizeof tmp, "%s", dir);
	  for (i = 1; tmp[i]; i++) if (tmp[i] == '/' || tmp[i] == '\\') { char sep = tmp[i]; tmp[i] = 0; SF_MKDIR(tmp); tmp[i] = sep; }
	  SF_MKDIR(tmp); }
	return dir;
}
static void fullname(char *out, size_t n, const char *prefix, const char *name)
{
	char clean[200]; size_t i, j = 0;
	snprintf(clean, sizeof clean, "%s%s", prefix, name);
	for (i = 0; clean[i] && j < sizeof clean - 1; i++) clean[j++] = (clean[i] == ':' || clean[i] == '/') ? '_' : clean[i];
	clean[j] = 0;
	snprintf(out, n, "%s/%s", savedir(), clean);
}
int32_t nvram_load (char *filename, char *gameprefix, char *addr, int32_t size)
{
	char p[700]; FILE *f; size_t got;
	fullname(p, sizeof p, gameprefix, filename);
	f = fopen(p, "rb");
	if (!f) return 0;
	got = fread(addr, 1, (size_t)size, f);
	fclose(f);
	return got == (size_t)size;
}
int32_t nvram_save (char *filename, char *gameprefix, char *addr, int32_t size)
{
	char p[700]; FILE *f; size_t put;
	fullname(p, sizeof p, gameprefix, filename);
	f = fopen(p, "wb");
	if (!f) return 0;
	put = fwrite(addr, 1, (size_t)size, f);
	fclose(f);
	return put == (size_t)size;
}
int32_t nvram_delete (char *filename, char *gameprefix)
{
	char p[700];
	fullname(p, sizeof p, gameprefix, filename);
	return remove(p) == 0;
}

#include <dirent.h>
int32_t nvram_directory (int32_t menu_number, int32_t menu_entry, int32_t menu_maxentry, int32_t set_itemok, char *gameprefix)
{
	DIR *d; struct dirent *e;
	int32_t loop, found = 0;
	char prefix[100]; size_t pl, i;
	for (loop = menu_entry; loop <= menu_maxentry; loop++) {
		menus[menu_number].items[loop].selectable = set_itemok;
		sprintf(message_decode(menus[menu_number].items[loop].string[0], 1), "-");
	}
	snprintf(prefix, sizeof prefix, "%s", gameprefix);
	for (i = 0; prefix[i]; i++) if (prefix[i] == ':' || prefix[i] == '/') prefix[i] = '_';
	pl = strlen(prefix);
	d = opendir(savedir());
	if (!d) return 0;
	while ((e = readdir(d)) != NULL && menu_entry <= menu_maxentry)
		if (strncmp(e->d_name, prefix, pl) == 0) {
			found++;
			strcpy(message_decode(menus[menu_number].items[menu_entry].string[0], 1), e->d_name + pl);
			menus[menu_number].items[menu_entry++].selectable = ITEM_OK;
		}
	closedir(d);
	return found;
}
