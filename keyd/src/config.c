/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "FreeRTOS.h"
#include "ff.h"

#include "config.h"
// #include "ini.h"
#include "keys.h"
#include "stringutils.h"
#include "porting.h"

struct ini_entry {
	char *key;
	char *val;

	size_t lnum;		// The line number in the original source file.
};

struct ini_section {
	char name[MAX_SECTION_NAME_LEN];
	// char name[1024];

	size_t nr_entries;
	size_t lnum;

	struct ini_entry entries[MAX_SECTION_ENTRIES];
};

const static struct {
	const char *name;
	const char *preferred_name;
	uint8_t op;
	enum {
		ARG_EMPTY,

		ARG_MACRO,
		ARG_LAYER,
		ARG_LAYOUT,
		ARG_TIMEOUT,
		ARG_SENSITIVITY,
		ARG_DESCRIPTOR,
	} args[MAX_DESCRIPTOR_ARGS];
} actions[] =  {
	{ "swap", 	NULL,	OP_SWAP,	{ ARG_LAYER } },
	{ "clear", 	NULL,	OP_CLEAR,	{} },
	{ "oneshot", 	NULL,	OP_ONESHOT,	{ ARG_LAYER } },
	{ "toggle", 	NULL,	OP_TOGGLE,	{ ARG_LAYER } },

	{ "clearm", 	NULL,	OP_CLEARM,	{ ARG_MACRO } },
	{ "swapm", 	NULL,	OP_SWAPM,	{ ARG_LAYER, ARG_MACRO } },
	{ "togglem", 	NULL,	OP_TOGGLEM,	{ ARG_LAYER, ARG_MACRO } },
	{ "layerm", 	NULL,	OP_LAYERM,	{ ARG_LAYER, ARG_MACRO } },
	{ "oneshotm", 	NULL,	OP_ONESHOTM,	{ ARG_LAYER, ARG_MACRO } },

	{ "layer", 	NULL,	OP_LAYER,	{ ARG_LAYER } },

	{ "overload", 	NULL,	OP_OVERLOAD,			{ ARG_LAYER, ARG_DESCRIPTOR } },
	{ "overloadt", 	NULL,	OP_OVERLOAD_TIMEOUT,		{ ARG_LAYER, ARG_DESCRIPTOR, ARG_TIMEOUT } },
	{ "overloadt2", NULL,	OP_OVERLOAD_TIMEOUT_TAP,	{ ARG_LAYER, ARG_DESCRIPTOR, ARG_TIMEOUT } },

	{ "overloadi",	NULL,	OP_OVERLOAD_IDLE_TIMEOUT, { ARG_DESCRIPTOR, ARG_DESCRIPTOR, ARG_TIMEOUT } },
	{ "timeout", 	NULL,	OP_TIMEOUT,	{ ARG_DESCRIPTOR, ARG_TIMEOUT, ARG_DESCRIPTOR } },

	{ "macro2", 	NULL,	OP_MACRO2,	{ ARG_TIMEOUT, ARG_TIMEOUT, ARG_MACRO } },
	{ "setlayout", 	NULL,	OP_LAYOUT,	{ ARG_LAYOUT } },

	/* Experimental */
	{ "scrollt", 	NULL,	OP_SCROLL_TOGGLE,		{ARG_SENSITIVITY} },
	{ "scroll", 	NULL,	OP_SCROLL,			{ARG_SENSITIVITY} },

	/* TODO: deprecate */
	{ "overload2", 	"overloadt",	OP_OVERLOAD_TIMEOUT,		{ ARG_LAYER, ARG_DESCRIPTOR, ARG_TIMEOUT } },
	{ "overload3", 	"overloadt2",	OP_OVERLOAD_TIMEOUT_TAP,	{ ARG_LAYER, ARG_DESCRIPTOR, ARG_TIMEOUT } },
	{ "toggle2", 	"togglem",	OP_TOGGLEM,			{ ARG_LAYER, ARG_MACRO } },
	{ "swap2", 	"swapm",	OP_SWAPM,			{ ARG_LAYER, ARG_MACRO } },
};


// static const char *resolve_include_path(const char *path, const char *include_path)
// {
// 	static char resolved_path[PATH_MAX];
// 	char tmp[PATH_MAX];
// 	const char *dir;

// 	if (strstr(include_path, ".")) {
// 		warn("%s: included files may not have a file extension", include_path);
// 		return NULL;
// 	}

// 	strcpy(tmp, path);
// 	dir = dirname(tmp);
// 	snprintf(resolved_path, sizeof resolved_path, "%s/%s", dir, include_path);

// 	if (!access(resolved_path, F_OK))
// 		return resolved_path;

// 	snprintf(resolved_path, sizeof resolved_path, DATA_DIR"/%s", include_path);

// 	if (!access(resolved_path, F_OK))
// 		return resolved_path;

// 	return NULL;
// }


// static char *read_file()
// {
//     char *buf = NULL;
//     // char line[MAX_LINE_LEN+1];
//     int buf_sz = MAX_LINE_LEN;     // Current size of the buffer
//     int total_sz = 0;   // Total size of the data read
	
// 	FATFS filesystem;
//     // Mount the filesystem
//     FRESULT res = f_mount(&filesystem, "/", 1);
//     if (res != FR_OK) {
//         printf("f_mount fail rc=%d\n", res);
//         return NULL;
//     }

//     FIL fh;
//     // Open the file for reading
//     res = f_open(&fh, "keyd.conf", FA_READ);
//     if (res != FR_OK) {
//         f_unmount("/");
//         printf("failed to open keyd.conf\n");
//         return NULL;
//     }

//     // Read file line by line
// 	char *line = (char *)malloc((MAX_LINE_LEN + 1) * sizeof(char));
//     while (f_gets(line, MAX_LINE_LEN + 1, &fh)) {
//         // Skip lines that start with '#'
//         if (line[0] == '#') {
//             continue;  // Ignore this line and go to the next one
//         }

//         int len = strlen(line);
// 		// printf("line length %d\n", len);

//         // Ensure the line ends with a newline character
//         if (line[len-1] != '\n') {
//             if (len >= MAX_LINE_LEN) {
//                 printf("maximum line length exceeded (%d)\n", MAX_LINE_LEN);
//                 goto fail;
//             } else {
//                 line[len++] = '\n';
//             }
//         }

//         if ((len+total_sz) > MAX_FILE_SZ) {
// 			printf("maximum file size exceed (%d)", MAX_FILE_SZ);
// 			goto fail;
// 		}
        
//         if (!total_sz) {
//             buf = (char *)malloc(len);
//             if (!buf) {
//                 printf("failed to allocate more memory for buf\n");
//                 goto fail;
//             }
//         } else {
//             buf_sz += len; 
//             char *new_buf = realloc(buf, buf_sz);
//             if (!new_buf) {
//                 printf("failed to allocate more memory for buf\n");
//                 goto fail;
//             }
//             buf = new_buf;
//         }

//         // str(line, include_prefix) == line) {
// 		// 	FIL *fd;
// 		// 	const char *resolved_path;
// 		// 	char *include_path = line+sizeof(include_prefix)-1;

// 		// 	line[len-1] = 0;

// 		// 	while (include_path[0] == ' ')
// 		// 		include_path++;

// 		// 	resolved_path = resolve_include_path(path, include_path);

// 		// 	if (!resolved_path) {
// 		// 		// warn("failed to resolve include path: %s", include_path);
// 		// 		continue;
// 		// 	}

// 		// 	fd = f_open(resolved_path, O_RDONLY);

// 		// 	if (fd < 0) {
// 		// 		// warn("failed to include %s", include_path);
// 		// 		// perror("open");
// 		// 	} else {
// 		// 		int n;
// 		// 		while ((n = f_read(fd, buf+sz, sizeof(buf)-sz)) > 0)
// 		// 			sz += n;
// 		// 		f_close(fd);
// 		// 	}
// 		// } else {

//         // Copy the line to the buffer
//         strcpy(buf + total_sz, line);
//         total_sz += len;
//     }

//     f_close(&fh);
//     f_unmount("/");
// 	free(line);

//     if (!buf) {
//         buf[total_sz] = '\0';  // Null-terminate the buffer
//     }
//     return buf;

// fail:
//     free(buf);  // Free the memory in case of failure
// 	free(line);

//     f_close(&fh);
//     f_unmount("/");
//     return NULL;
// }


/* Return up to two keycodes associated with the given name. */
static uint8_t lookup_keycode(const char *name)
{
	size_t i;

	for (i = 0; i < 256; i++) {
		const struct keycode_table_ent *ent = &keycode_table[i];

		if (ent->name &&
		    (!strcmp(ent->name, name) ||
		     (ent->alt_name && !strcmp(ent->alt_name, name)))) {
			return i;
		}
	}

	return 0;
}

static struct descriptor *layer_lookup_chord(struct layer *layer, uint8_t *keys, size_t n)
{
    size_t i;

    for (i = 0; i < layer->nr_chords; i++) {
        size_t j;
        size_t nm = 0;
        struct chord *chord = &layer->chords[i];

        for (j = 0; j < n; j++) {
            size_t k;
            for (k = 0; k < chord->sz; k++)
                if (keys[j] == chord->keys[k]) {
                    nm++;
                    break;
                }
        }

        if (nm == n)
            return &chord->d;  // No change needed
    }

    return NULL;
}

static int add_chord_to_layer(struct layer *layer, const struct chord *new_chord)
{
    struct chord *temp = realloc(layer->chords, (layer->nr_chords + 1) * sizeof(struct chord));
    if (!temp) {
        err("Failed to allocate memory for chords");
        return -1;
    }

    layer->chords = temp;
    layer->chords[layer->nr_chords++] = *new_chord;

    return 0;
}

/*
 * Consumes a string of the form `[<layer>.]<key> = <descriptor>` and adds the
 * mapping to the corresponding layer in the config.
 */
static int set_layer_entry(const struct config *config,
                           struct layer *layer, char *key,
                           const struct descriptor *d)
{
    size_t i;
    int found = 0;

    if (strchr(key, '+')) {
        // Handle chorded keys
        char *tok;
        struct descriptor *ld;
        uint8_t keys[ARRAY_SIZE(layer->chords[0].keys)];
        size_t n = 0;

        for (tok = strtok(key, "+"); tok; tok = strtok(NULL, "+")) {
            uint8_t code = lookup_keycode(tok);
            if (!code) {
                err("%s is not a valid key", tok);
                return -1;
            }

            if (n >= ARRAY_SIZE(keys)) {
                err("chords cannot contain more than %ld keys", n);
                return -1;
            }

            keys[n++] = code;
        }

        if ((ld = layer_lookup_chord(layer, keys, n))) {
            *ld = *d;
        } else {
            struct chord new_chord;
            if (n > ARRAY_SIZE(new_chord.keys)) {
                err("chords cannot contain more than %ld keys", ARRAY_SIZE(new_chord.keys));
                return -1;
            }

            memcpy(new_chord.keys, keys, n * sizeof(uint8_t));
            new_chord.sz = n;
            new_chord.d = *d;

            if (add_chord_to_layer(layer, &new_chord) < 0)
                return -1;
        }
    } else {
        for (i = 0; i < 256; i++) {
            if (config->aliases[i] && !strcmp(config->aliases[i], key)) {
                // Allocate memory if not already allocated
                if (layer->keymap[i] == NULL) {
                    layer->keymap[i] = malloc(sizeof(struct descriptor));
                    if (!layer->keymap[i]) {
                        err("Memory allocation failed for keymap[%zu]", i);
                        return -1;
                    }
                }
                *(layer->keymap[i]) = *d;
                found = 1;
            }
        }

        if (!found) {
            uint8_t code;

            if (!(code = lookup_keycode(key))) {
                err("%s is not a valid key or alias", key);
                return -1;
            }

            // Allocate memory if not already allocated
            if (layer->keymap[code] == NULL) {
                layer->keymap[code] = malloc(sizeof(struct descriptor));
                if (!layer->keymap[code]) {
                    err("Memory allocation failed for keymap[%u]", code);
                    return -1;
                }
            }

            *(layer->keymap[code]) = *d;
        }
    }

    return 0;
}

static int new_layer(char *s, const struct config *config, struct layer *layer)
{
    uint8_t mods;
    char *name;
    char *type;
    size_t i;

    name = strtok(s, ":");
    type = strtok(NULL, ":");

    strcpy(layer->name, name);

    layer->nr_chords = 0;

    // Initialize keymap pointers to NULL
    for (i = 0; i < 256; i++) {
        layer->keymap[i] = NULL;
    }
    layer->nr_chords = 0;
    layer->chords = NULL;

    if (strchr(name, '+')) {
        char *layername;
        int n = 0;

        layer->type = LT_COMPOSITE;
        layer->nr_constituents = 0;

        if (type) {
            // printf("composite layers cannot have a type.");
            err("composite layers cannot have a type.");
            return -1;
        }

        for (layername = strtok(name, "+"); layername; layername = strtok(NULL, "+")) {
            int idx = config_get_layer_index(config, layername);

            if (idx < 0) {
                // printf("%s is not a valid layer", layername);
                err("%s is not a valid layer", layername);
                return -1;
            }

            if (n >= ARRAY_SIZE(layer->constituents)) {
                err("max composite layers (%d) exceeded", ARRAY_SIZE(layer->constituents));
                // printf("max composite layers (%d) exceeded", ARRAY_SIZE(layer->constituents));
                return -1;
            }

            layer->constituents[layer->nr_constituents++] = idx;
        }

    } else if (type && !strcmp(type, "layout")) {
            layer->type = LT_LAYOUT;
    } else if (type && !parse_modset(type, &mods)) {
            layer->type = LT_NORMAL;
            layer->mods = mods;
    } else {
        if (type)
            warn("\"%s\" is not a valid layer type, ignoring\n", type);
            // printf("\"%s\" is not a valid layer type, ignoring\n", type);

        layer->type = LT_NORMAL;
        layer->mods = 0;
    }

    return 0;
}

/*
 * Returns:
 * 	1 if the layer exists
 * 	0 if the layer was created successfully
 * 	< 0 on error
 */
static int config_add_layer(struct config *config, const char *s)
{
	int ret;
	char buf[MAX_LAYER_NAME_LEN+1];
	char *name;

	if (strlen(s) >= sizeof buf) {
		err("%s exceeds maximum section length (%d) (ignoring)", s, MAX_LAYER_NAME_LEN);
		return -1;
	}

	strcpy(buf, s);
	name = strtok(buf, ":");

	if (config_get_layer_index(config, name) != -1)
			return 1;

	if (config->nr_layers >= MAX_LAYERS) {
		// printf("max layers (%d) exceeded", MAX_LAYERS);
		err("max layers (%d) exceeded", MAX_LAYERS);
		return -1;
	}

	strcpy(buf, s);
	ret = new_layer(buf, config, &config->layers[config->nr_layers]);

	if (ret < 0)
		return -1;

	config->nr_layers++;
	return 0;
}

/* Modifies the input string */
static int parse_fn(char *s,
		    char **name,
		    char *args[5],
		    size_t *nargs)
{
	char *c, *arg;

	c = s;
	while (*c && *c != '(')
		c++;

	if (!*c)
		return -1;

	*name = s;
	*c++ = 0;

	while (*c == ' ')
		c++;

	*nargs = 0;
	arg = c;
	while (1) {
		int plvl = 0;

		while (*c) {
			switch (*c) {
			case '\\':
				if (*(c+1)) {
					c+=2;
					continue;
				}
				break;
			case '(':
				plvl++;
				break;
			case ')':
				plvl--;

				if (plvl == -1)
					goto exit;
				break;
			case ',':
				if (plvl == 0)
					goto exit;
				break;
			}

			c++;
		}
exit:

		if (!*c)
			return -1;

		if (arg != c) {
			assert(*nargs < 5);
			args[(*nargs)++] = arg;
		}

		if (*c == ')') {
			*c = 0;
			return 0;
		}

		*c++ = 0;
		while (*c == ' ')
			c++;
		arg = c;
	}
}

/*
 * Parses macro expression placing the result
 * in the supplied macro struct.
 *
 * Returns:
 *   0 on success
 *   -1 in the case of an invalid macro expression
 *   > 0 for all other errors
 */

// int parse_macro_expression(const char *s, struct macro *macro)
// {
// 	uint8_t code, mods;
// 	// err("maximum macro size (%d) exceeded", ARRAY_SIZE(macro->entries));
// 	#define ADD_ENTRY(t, d) do { \
// 		if (macro->sz >= ARRAY_SIZE(macro->entries)) { \
// 			printf("maximum macro size (%d) exceeded", ARRAY_SIZE(macro->entries)); \
// 			return 1; \
// 		} \
// 		macro->entries[macro->sz].type = t; \
// 		macro->entries[macro->sz].data = d; \
// 		macro->sz++; \
// 	} while(0)

// 	size_t len = strlen(s);

// 	// char buf[1024];
// 	// char *ptr = buf;

// 	if (len >= 1024) {
// 		printf("macro size exceeded maximum size (%ld)\n", 1024);
// 		// err("macro size exceeded maximum size (%ld)\n", 1024);
// 		return -1;
// 	}
// 	char *ptr = malloc(len + 1); // change to dynamic allocation

// 	strcpy(ptr, s);

// 	if (!strncmp(ptr, "macro(", 6) && ptr[len-1] == ')') {
// 		ptr[len-1] = 0;
// 		ptr += 6;
// 		str_escape(ptr);
// 	} else if (parse_key_sequence(ptr, &code, &mods) && utf8_strlen(ptr) != 1) {
// 		printf("Invalid macro");
// 		// err("Invalid macro");
// 		free(ptr);
// 		return -1;
// 	}

// 	return macro_parse(ptr, macro) == 0 ? 0 : 1;
// }

int parse_macro_expression(const char *s, struct macro *macro)
{
	uint8_t code, mods;

	#define ADD_ENTRY(t, d) do { \
		if (macro->sz >= ARRAY_SIZE(macro->entries)) { \
			printf("maximum macro size (%d) exceeded\n", ARRAY_SIZE(macro->entries)); \
			return 1; \
		} \
		macro->entries[macro->sz].type = t; \
		macro->entries[macro->sz].data = d; \
		macro->sz++; \
	} while(0)

	size_t len = strlen(s);

	char buf[MAX_EXP_LEN];
	char *ptr = buf;

	if (len >= sizeof(buf)) {
		// printf("macro size exceeded maximum size (%ld)\n", sizeof(buf));
		err("macro size exceeded maximum size (%ld)\n", sizeof(buf));
		return -1;
	}

	strcpy(buf, s);

	if (!strncmp(ptr, "macro(", 6) && ptr[len-1] == ')') {
		ptr[len-1] = 0;
		ptr += 6;
		str_escape(ptr);
	} else if (parse_key_sequence(ptr, &code, &mods) && utf8_strlen(ptr) != 1) {
		// printf("Invalid macro\n");
		err("Invalid macro %s and %s", s, ptr);
		return -1;
	}

	return macro_parse(ptr, macro) == 0 ? 0 : 1;
}

static int parse_command(const char *s)
// static int parse_command(const char *s, struct command *command)
{
	// return -1;
	int len = strlen(s);

	if (len == 0 || strstr(s, "command(") != s || s[len-1] != ')')
		return -1;

    // TODO: potential way to format flash storage
	// if (len > (int)sizeof(command->cmd)) {
	// 	printf("max command length (%ld) exceeded\n", sizeof(command->cmd));
	// 	// err("max command length (%ld) exceeded\n", sizeof(command->cmd));
	// 	return 1;
	// }

	// strcpy(command->cmd, s+8);
	// command->cmd[len-9] = 0;
	// str_escape(command->cmd);

	return 0;
}

static int parse_descriptor(char *s,
			    struct descriptor *d,
			    struct config *config)
{
	char *fn = NULL;
	char *args[5];
	size_t nargs = 0;
	uint8_t code, mods;
	int ret;
	struct macro macro;
	// struct command *cmd = NULL;

	if (!s || !s[0]) {
		d->op = 0;
		printf("d->op = 0\n");

		return 0;
	}
	printf("parsing dsc\n");
	printf("s = %s\n", s);
	

	if (!parse_key_sequence(s, &code, &mods)) {
		size_t i;
		const char *layer = NULL;

		switch (code) {
			case KEYD_LEFTSHIFT:   layer = "shift"; break;
			case KEYD_LEFTCTRL:    layer = "control"; break;
			case KEYD_LEFTMETA:    layer = "meta"; break;
			case KEYD_LEFTALT:     layer = "alt"; break;
			case KEYD_RIGHTALT:    layer = "altgr"; break;
		}

		if (layer) {
			warn("You should use b{layer(%s)} instead of assigning to b{%s} directly.", layer, KEY_NAME(code));
			d->op = OP_LAYER;
			d->args[0].idx = config_get_layer_index(config, layer);

			assert(d->args[0].idx != -1);

			return 0;
		}

		d->op = OP_KEYSEQUENCE;
		d->args[0].code = code;
		d->args[1].mods = mods;

		return 0;
	} else if ((ret=parse_command(s)) >= 0) {
	// } else if ((ret=parse_command(s, cmd)) >= 0) {
		if (ret) {
			return -1;
		}

		// if (config->nr_commands >= ARRAY_SIZE(config->commands)) {
		// 	// err("max commands (%d), exceeded", ARRAY_SIZE(config->commands));
		// 	printf("max commands (%d), exceeded", ARRAY_SIZE(config->commands));
		// 	return -1;
		// }


		// d->op = OP_COMMAND;
		// d->args[0].idx = config->nr_commands;

		// config->commands[config->nr_commands++] = cmd;

		return 0;
	} else if ((ret=parse_macro_expression(s, &macro)) >= 0) {
		if (ret)
			return -1;

		if (config->nr_macros >= ARRAY_SIZE(config->macros)) {
			// printf("max macros (%d), exceeded\n", ARRAY_SIZE(config->macros));
			err("max macros (%d), exceeded", ARRAY_SIZE(config->macros));
			return -1;
		}

		d->op = OP_MACRO;
		d->args[0].idx = config->nr_macros;

		config->macros[config->nr_macros++] = macro;

		return 0;
	} else if (!parse_fn(s, &fn, args, &nargs)) {
		int i;

		if (!strcmp(fn, "lettermod")) {
			char buf[1024];

			if (nargs != 4) {
				err("%s requires 4 arguments", fn);
				// printf("%s requires 4 arguments\n", fn);
				return -1;
			}

			snprintf(buf, sizeof buf,
				"overloadi(%s, overloadt2(%s, %s, %s), %s)",
				args[1], args[0], args[1], args[3], args[2]);

			if (parse_fn(buf, &fn, args, &nargs)) {
				err("failed to parse %s", buf);
				// printf("failed to parse %s\n", buf);
				return -1;
			}
		}

		for (i = 0; i < ARRAY_SIZE(actions); i++) {
			if (!strcmp(actions[i].name, fn)) {
				int j;

				if (actions[i].preferred_name)
					warn("%s is deprecated (renamed to %s).", actions[i].name, actions[i].preferred_name);

				d->op = actions[i].op;

				for (j = 0; j < MAX_DESCRIPTOR_ARGS; j++) {
					if (!actions[i].args[j])
						break;
				}

				if ((int)nargs != j) {
					// printf("%s requires %d %s\n", actions[i].name, j, j == 1 ? "argument" : "arguments");
					err("%s requires %d %s", actions[i].name, j, j == 1 ? "argument" : "arguments");
					return -1;
				}

				while (j--) {
					int type = actions[i].args[j];
					union descriptor_arg *arg = &d->args[j];
					char *argstr = args[j];
					struct descriptor desc;

					switch (type) {
					case ARG_LAYER:
						if (!strcmp(argstr, "main")) {
							// printf("the main layer cannot be toggled\n");
							err("the main layer cannot be toggled");
							return -1;
						}

						arg->idx = config_get_layer_index(config, argstr);
						if (arg->idx == -1 || config->layers[arg->idx].type == LT_LAYOUT) {
							// printf("%s is not a valid layer", argstr);
							err("%s is not a valid layer", argstr);
							return -1;
						}

						break;
					case ARG_LAYOUT:
						arg->idx = config_get_layer_index(config, argstr);
						if (arg->idx == -1 ||
							(arg->idx != 0 && //Treat main as a valid layout
							 config->layers[arg->idx].type != LT_LAYOUT)) {
							printf("%s is not a valid layout", argstr);
							// err("%s is not a valid layout", argstr);
							return -1;
						}

						break;
					case ARG_DESCRIPTOR:
						if (parse_descriptor(argstr, &desc, config))
							return -1;

						if (config->nr_descriptors >= ARRAY_SIZE(config->descriptors)) {
							// printf("maximum descriptors exceeded");
							err("maximum descriptors exceeded");
							return -1;
						}

						config->descriptors[config->nr_descriptors] = desc;
						arg->idx = config->nr_descriptors++;
						break;
					case ARG_SENSITIVITY:
						arg->sensitivity = atoi(argstr);
						break;
					case ARG_TIMEOUT:
						arg->timeout = atoi(argstr);
						break;
					case ARG_MACRO:
						if (config->nr_macros >= ARRAY_SIZE(config->macros)) {
							// printf("max macros (%d), exceeded", ARRAY_SIZE(config->macros));
							err("max macros (%d), exceeded", ARRAY_SIZE(config->macros));
							return -1;
						}

						if (parse_macro_expression(argstr, &config->macros[config->nr_macros])) {
							return -1;
						}

						arg->idx = config->nr_macros;
						config->nr_macros++;

						break;
					default:
						assert(0);
						break;
					}
				}

				return 0;
			}
		}
	}

	printf("invalid key or action");
	// err("invalid key or action");
	return -1;
}

static void parse_global_section(struct config *config, struct ini_section *section)
{
	size_t i;

	for (i = 0; i < section->nr_entries;i++) {
		struct ini_entry *ent = &section->entries[i];

		if (!strcmp(ent->key, "macro_timeout"))
			config->macro_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "macro_sequence_timeout"))
			config->macro_sequence_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "disable_modifier_guard"))
			config->disable_modifier_guard = atoi(ent->val);
		else if (!strcmp(ent->key, "oneshot_timeout"))
			config->oneshot_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "chord_hold_timeout"))
			config->chord_hold_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "chord_timeout"))
			config->chord_interkey_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "default_layout"))
			snprintf(config->default_layout, sizeof config->default_layout,
				 "%s", ent->val);
		else if (!strcmp(ent->key, "macro_repeat_timeout"))
			config->macro_repeat_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "layer_indicator"))
			config->layer_indicator = atoi(ent->val);
		else if (!strcmp(ent->key, "overload_tap_timeout"))
			config->overload_tap_timeout = atoi(ent->val);
		else
			printf("line %zd: %s is not a valid global option\n", ent->lnum, ent->key);
	}
}

static void parse_id_section(struct config *config, struct ini_section *section)
{
	// size_t i;
	// for (i = 0; i < section->nr_entries; i++) {
	// 	uint16_t product, vendor;

	// 	struct ini_entry *ent = &section->entries[i];
	// 	const char *s = ent->key;

		// if (!strcmp(s, "*")) {
		// 	config->wildcard = 1;
		// } else if (strstr(s, "m:") == s) {
		// 	assert(config->nr_ids < ARRAY_SIZE(config->ids));
		// 	config->ids[config->nr_ids].flags = ID_MOUSE;

		// 	snprintf(config->ids[config->nr_ids++].id, sizeof(config->ids[0].id), "%s", s+2);
		// } else if (strstr(s, "k:") == s) {
		// 	assert(config->nr_ids < ARRAY_SIZE(config->ids));
		// 	config->ids[config->nr_ids].flags = ID_KEYBOARD;

		// 	snprintf(config->ids[config->nr_ids++].id, sizeof(config->ids[0].id), "%s", s+2);
		// } else if (strstr(s, "-") == s) {
		// 	assert(config->nr_ids < ARRAY_SIZE(config->ids));
		// 	config->ids[config->nr_ids].flags = ID_EXCLUDED;

		// 	snprintf(config->ids[config->nr_ids++].id, sizeof(config->ids[0].id), "%s", s+1);
		// } else if (strlen(s) < sizeof(config->ids[config->nr_ids].id)-1) {
		// 	assert(config->nr_ids < ARRAY_SIZE(config->ids));
		// 	config->ids[config->nr_ids].flags = ID_KEYBOARD | ID_MOUSE;

		// 	snprintf(config->ids[config->nr_ids++].id, sizeof(config->ids[0].id), "%s", s);
		// } else {
		// 	// warn("%s is not a valid device id", s);
		// 	printf("%s is not a valid device id", s);
		// }
	// }
}

static void parse_alias_section(struct config *config, struct ini_section *section)
{
    size_t i;

    for (i = 0; i < section->nr_entries; i++) {
        uint8_t code;
        struct ini_entry *ent = &section->entries[i];
        const char *name = ent->val;

        if ((code = lookup_keycode(ent->key))) {
            size_t len = strlen(name);

            // Optionally enforce a maximum alias length
            if (len >= MAX_ALIAS_LENGTH) {
                warn("%s exceeds the maximum alias length (%d)", name, MAX_ALIAS_LENGTH - 1);
                continue;  // Skip setting this alias
            }

            uint8_t alias_code;

            if ((alias_code = lookup_keycode(name))) {
                // Allocate memory if not already allocated
                if (config->layers[0].keymap[code] == NULL) {
                    config->layers[0].keymap[code] = malloc(sizeof(struct descriptor));
                    if (!config->layers[0].keymap[code]) {
                        warn("Memory allocation failed for alias keymap[%u]", code);
                        continue;
                    }
                }

                struct descriptor *d = config->layers[0].keymap[code];
                d->op = OP_KEYSEQUENCE;
                d->args[0].code = alias_code;
                d->args[1].mods = 0;
            }

            // Free existing alias if it exists to prevent memory leaks
            if (config->aliases[code]) {
                free(config->aliases[code]);
                config->aliases[code] = NULL;
            }

            // Allocate memory for the new alias
            config->aliases[code] = strdup(name);
            if (!config->aliases[code]) {
                warn("Failed to allocate memory for alias '%s'", name);
            }
        } else {
            printf("Failed to define alias '%s', '%s' is not a valid keycode\n", name, ent->key);
        }
    }
}


// static int config_parse_string(struct config *config, char *content)
// {
// 	size_t i;
// 	struct ini *ini;

// 	if (!(ini = ini_parse_string(content, NULL)))
// 		return -1;

// 	/* First pass: create all layers based on section headers.  */
// 	for (i = 0; i < ini->nr_sections; i++) {
// 		struct ini_section *section = &ini->sections[i];

// 		if (!strcmp(section->name, "ids")) {
// 			parse_id_section(config, section);
// 			printf("id section is not supported\n");
// 		} else if (!strcmp(section->name, "aliases")) {
// 			parse_alias_section(config, section);
// 		} else if (!strcmp(section->name, "global")) {
// 			parse_global_section(config, section);
// 		} else {
// 			if (config_add_layer(config, section->name) < 0)
// 				warn("some warm");
// 		}
// 	}

// 	/* Populate each layer. */
// 	for (i = 0; i < ini->nr_sections; i++) {
// 		size_t j;
// 		char *layername;
// 		struct ini_section *section = &ini->sections[i];

// 		if (!strcmp(section->name, "ids") ||
// 		    !strcmp(section->name, "aliases") ||
// 		    !strcmp(section->name, "global"))
// 			continue;

// 		layername = strtok(section->name, ":");

// 		for (j = 0; j < section->nr_entries;j++) {
// 			char entry[MAX_EXP_LEN];
// 			struct ini_entry *ent = &section->entries[j];

// 			if (!ent->val) {
// 				warn("invalid binding on line %zd", ent->lnum);
// 				continue;
// 			}

// 			snprintf(entry, sizeof entry, "%s.%s = %s", layername, ent->key, ent->val);

// 			if (config_add_entry(config, entry) < 0) {
// 				keyd_log("\tr{ERROR:} line m{%zd}\n", ent->lnum);
// 				printf("%s failed\n", entry);
// 			} else {
// 				printf("%s entry add\n", entry);
// 			}
// 		}
// 	}

// 	return 0;
// }

void free_section_entries(struct ini_section *section)
{
    for (size_t i = 0; i < section->nr_entries; i++) {
        free(section->entries[i].key);
        free(section->entries[i].val);
    }
    section->nr_entries = 0;
}
int process_section(struct ini_section *section, struct config *config) {
    if (!strcmp(section->name, "ids")) {
        parse_id_section(config, section);
        printf("ids section processed\n");
    } else if (!strcmp(section->name, "aliases")) {
        parse_alias_section(config, section);
        printf("aliases section processed\n");
    } else if (!strcmp(section->name, "global")) {
        parse_global_section(config, section);
        printf("global section processed\n");
    } else {
        if (config_add_layer(config, section->name) < 0) {
            printf("Error adding layer '%s'\n", section->name);
            return -1;
        }
        printf("Layer '%s' added\n", section->name);
    }
    return 0;
}

void parse_kvp(char *s, char **key, char **value)
{
	char *last_space = NULL;
	char *c = s;

	/* Allow the first character to be = as a special case. */
	if (*c == '=')
		c++;

	*key = s;
	*value = NULL;
	while (*c) {
		switch (*c) {
		case '=':
			if (last_space)
				*last_space = 0;
			else
				*c = 0;

			while (*++c == ' ' || *c == '\t');

			*value = c;
			return;
		case ' ':
		case '\t':
			if (!last_space)
				last_space = c;
			break;
		default:
			last_space = NULL;
			break;
		}

		c++;
	}
}

int config_parse_file(struct config *config)
{
    FATFS filesystem;
    FRESULT res;
    FIL fh;
    char line[MAX_LINE_LEN + 1];
    size_t ln = 0;
    struct ini_section section;
    memset(&section, 0, sizeof(section));

    // Mount the filesystem
    res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK) {
        printf("f_mount fail rc=%d\n", res);
        return -1;
    }

    // First pass: Create all layers based on section headers
    res = f_open(&fh, "keyd.conf", FA_READ);
    if (res != FR_OK) {
        f_unmount("/");
        printf("failed to open keyd.conf\n");
        return -1;
    }

    printf("Starting first pass...\n");

    while (f_gets(line, MAX_LINE_LEN + 1, &fh)) {
        ln++;

        // Remove leading whitespace
        char *ptr = line;
        while (isspace((unsigned char)*ptr))
            ptr++;

        size_t len = strlen(ptr);

        // Remove trailing whitespace
        while (len > 0 && isspace((unsigned char)ptr[len - 1]))
            len--;

        if (len == 0)
            continue; // Empty line

        ptr[len] = '\0'; // Null-terminate the trimmed line

        printf("Line %zd: '%s'\n", ln, ptr);

        if (ptr[0] == '#')
            continue; // Comment line

        if (ptr[0] == '[') {
            // Check if it's a valid section header
            if (len > 2 && ptr[len - 1] == ']') {
                // Process previous section if any
                if (section.name[0] != '\0') {
                    printf("Processing section '%s' at line %zd\n", section.name, section.lnum);

                    if (process_section(&section, config) < 0) {
                        free_section_entries(&section);
                        f_close(&fh);
                        f_unmount("/");
                        return -1;
                    }

                    // Free duplicated entries
                    free_section_entries(&section);

                    // Reset section
                    memset(&section, 0, sizeof(section));
                }

                // Start new section
                memset(&section, 0, sizeof(section)); // Ensure section is reset
                section.lnum = ln;
                ptr[len - 1] = '\0'; // Remove closing ']'
                strncpy(section.name, ptr + 1, sizeof(section.name) - 1);
                section.name[sizeof(section.name) - 1] = '\0'; // Ensure null termination

                printf("New section: '%s' at line %zd\n", section.name, ln);

                continue;
            }
        }

        // Process entry
        if (!strcmp(section.name, "ids") ||
            !strcmp(section.name, "aliases") ||
            !strcmp(section.name, "global")) {

            if (section.nr_entries >= MAX_SECTION_ENTRIES) {
                printf("Error: Too many entries in section '%s'\n", section.name);
                free_section_entries(&section);
                f_close(&fh);
                f_unmount("/");
                return -1;
            }

            // Parse key-value pair
            char *key = NULL;
            char *val = NULL;
            parse_kvp(ptr, &key, &val);

            if (!key) {
                printf("Error parsing key at line %zd\n", ln);
                free_section_entries(&section);
                f_close(&fh);
                f_unmount("/");
                return -1;
            }

            // Duplicate key and val
            struct ini_entry *ent = &section.entries[section.nr_entries++];
            ent->lnum = ln;
            ent->key = strdup(key);
            ent->val = val ? strdup(val) : NULL;

            if (!ent->key || (val && !ent->val)) {
                printf("Memory allocation failed at line %zd\n", ln);
                free_section_entries(&section);
                f_close(&fh);
                f_unmount("/");
                return -1;
            }

            printf("Parsed entry: key='%s', val='%s' at line %zd\n", ent->key, ent->val ? ent->val : "(null)", ln);
        } else {
            // For other sections, we don't need to store entries in the first pass
            // We can skip processing the entry
        }
    }

    // Process the last section if any
    if (section.name[0] != '\0') {
        printf("Processing section '%s' at line %zd\n", section.name, section.lnum);

        if (process_section(&section, config) < 0) {
			free_section_entries(&section);
			f_close(&fh);
			f_unmount("/");
			return -1;
		}
        free_section_entries(&section);
        memset(&section, 0, sizeof(section));
    }
    f_close(&fh);

    // Second pass: Populate each layer
    ln = 0;
    res = f_open(&fh, "keyd.conf", FA_READ);
    if (res != FR_OK) {
        f_unmount("/");
        printf("failed to open keyd.conf\n");
        return -1;
    }

    printf("Starting second pass...\n");

    // memset(&section, 0, sizeof(section)); // Ensure section is reset

	char section_name[MAX_SECTION_NAME_LEN+1];
	section_name[0] = '\0';

    while (f_gets(line, MAX_LINE_LEN + 1, &fh)) {
        ln++;

        // Remove leading whitespace
        char *ptr = line;
        while (isspace((unsigned char)*ptr))
            ptr++;

        size_t len = strlen(ptr);

        // Remove trailing whitespace
        while (len > 0 && isspace((unsigned char)ptr[len - 1]))
            len--;

        if (len == 0)
            continue; // Empty line

        ptr[len] = '\0'; // Null-terminate the trimmed line

        printf("Line %zd: '%s'\n", ln, ptr);

        if (ptr[0] == '#')
            continue; // Comment line

        if (ptr[0] == '[') {
            // // Check if it's a valid section header
            if (len > 2 && ptr[len - 1] == ']') {
                ptr[len - 1] = '\0'; // Remove closing ']'
                snprintf(section_name, sizeof(section_name), "%s", ptr + 1);
                printf("New section: '%s' at line %zd\n", section_name, ln);
                continue;
            }
        }

        // Process entry
        if (!strcmp(section_name, "ids") ||
		    !strcmp(section_name, "aliases") ||
		    !strcmp(section_name, "global") ||
			section_name[0] == '\0')
			continue;

		// Parse key-value pair
		char *key = NULL;
		char *val = NULL;
		parse_kvp(ptr, &key, &val);

		char layername[MAX_SECTION_NAME_LEN + 1];
		strncpy(layername, section_name, sizeof(layername) - 1);
		layername[sizeof(layername) - 1] = '\0';

		char *token = strtok(layername, ":");
		char entry[MAX_EXP_LEN];
		snprintf(entry, sizeof(entry), "%s.%s = %s", token, key, val);

		if (config_add_entry(config, entry) < 0) {
			printf("Error adding entry '%s' at line %zd\n", entry, ln);
		} else {
			printf("Added entry '%s'\n", entry);
		}
    }

    f_close(&fh);
    f_unmount("/");
    printf("Configuration parsing completed successfully.\n");

    return 0;
}

// static void config_init(struct config *config)
// {
// 	size_t i;
// 	// printf("cinit bm heap size: %u bytes\n", xPortGetFreeHeapSize());

// 	memset(config, 0, sizeof *config);
// 	// printf("cinit am heap size: %u bytes\n", xPortGetFreeHeapSize());

// 	char default_config[] =
// 	"[aliases]\n"

// 	"leftshift = shift\n"
// 	"rightshift = shift\n"

// 	"leftalt = alt\n"
// 	"rightalt = altgr\n"

// 	"leftmeta = meta\n"
// 	"rightmeta = meta\n"

// 	"leftcontrol = control\n"
// 	"rightcontrol = control\n"

// 	"[main]\n"

// 	"shift = layer(shift)\n"
// 	"alt = layer(alt)\n"
// 	"altgr = layer(altgr)\n"
// 	"meta = layer(meta)\n"
// 	"control = layer(control)\n"

// 	"[control:C]\n"
// 	"[shift:S]\n"
// 	"[meta:M]\n"
// 	"[alt:A]\n"
// 	"[altgr:G]\n";

// 	// printf("before config_parse_string in config_init heap size: %u bytes\n", xPortGetFreeHeapSize());


// 	config_parse_string(config, default_config);

// 	/* In ms */
// 	config->chord_interkey_timeout = 50;
// 	config->chord_hold_timeout = 0;
// 	config->oneshot_timeout = 0;

// 	config->macro_timeout = 600;
// 	config->macro_repeat_timeout = 50;
// }

int config_init_(struct config *config) {
	config->chord_interkey_timeout = 50;
	config->chord_hold_timeout = 0;
	config->oneshot_timeout = 0;

	config->macro_timeout = 600;
	config->macro_repeat_timeout = 50;

	const char *config_lines[] = {
		"[aliases]",
		"leftshift = shift",
		"rightshift = shift",
		"",
		"leftalt = alt",
		"rightalt = altgr",
		"",
		"leftmeta = meta",
		"rightmeta = meta",
		"",
		"leftcontrol = control",
		"rightcontrol = control",
		"",
		"[main]",
		"shift = layer(shift)",
		"alt = layer(alt)",
		"altgr = layer(altgr)",
		"meta = layer(meta)",
		"control = layer(control)",
		"",
		"[control:C]",
		"[shift:S]",
		"[meta:M]",
		"[alt:A]",
		"[altgr:G]",
		NULL  // Sentinel to mark the end of the array
	};
    // First Pass: Create all layers based on section headers
    printf("Starting first pass...\n");

    struct ini_section section;
    memset(&section, 0, sizeof(section));

    size_t ln = 0;
    for (size_t i = 0; config_lines[i] != NULL; i++) {
        ln++;
        char line_buffer[MAX_LINE_LEN + 1];
        strncpy(line_buffer, config_lines[i], MAX_LINE_LEN);
        line_buffer[MAX_LINE_LEN] = '\0';  // Ensure null termination
        char *line = line_buffer;

        // Remove leading whitespace
        while (isspace((unsigned char)*line))
            line++;

        size_t len = strlen(line);

        // Remove trailing whitespace
        while (len > 0 && isspace((unsigned char)line[len - 1]))
            len--;

        if (len == 0)
            continue; // Empty line

        line[len] = '\0'; // Null-terminate the trimmed line

        printf("Line %zu: '%s'\n", ln, line);

        if (line[0] == '#')
            continue; // Comment line

        if (line[0] == '[') {
            // Check if it's a valid section header
            if (len > 2 && line[len - 1] == ']') {
                // Process previous section if any
                if (section.name[0] != '\0') {
                    printf("Processing section '%s' at line %zu\n", section.name, section.lnum);

                    if (process_section(&section, config) < 0) {
                        free_section_entries(&section);
                        return -1;
                    }

                    // Free duplicated entries
                    free_section_entries(&section);

                    // Reset section
                    memset(&section, 0, sizeof(section));
                }

                // Start new section
                memset(&section, 0, sizeof(section)); // Ensure section is reset
                section.lnum = ln;
                line[len - 1] = '\0'; // Remove closing ']'
                strncpy(section.name, line + 1, sizeof(section.name) - 1);
                section.name[sizeof(section.name) - 1] = '\0'; // Ensure null termination

                printf("New section: '%s' at line %zu\n", section.name, ln);

                continue;
            }
        }

        // Process entry
        if (!strcmp(section.name, "ids") ||
            !strcmp(section.name, "aliases") ||
            !strcmp(section.name, "global")) {

            if (section.nr_entries >= MAX_SECTION_ENTRIES) {
                printf("Error: Too many entries in section '%s'\n", section.name);
                free_section_entries(&section);
                return -1;
            }

            // Parse key-value pair
            char *key = NULL;
            char *val = NULL;
            parse_kvp(line, &key, &val);

            if (!key) {
                printf("Error parsing key at line %zu\n", ln);
                free_section_entries(&section);
                return -1;
            }

            // Duplicate key and val
            struct ini_entry *ent = &section.entries[section.nr_entries++];
            ent->lnum = ln;
            ent->key = strdup(key);
            ent->val = val ? strdup(val) : NULL;

            if (!ent->key || (val && !ent->val)) {
                printf("Memory allocation failed at line %zu\n", ln);
                free_section_entries(&section);
                return -1;
            }

            printf("Parsed entry: key='%s', val='%s' at line %zu\n", ent->key, ent->val ? ent->val : "(null)", ln);
        } else {
            // For other sections, we don't need to store entries in the first pass
            // We can skip processing the entry
        }
    }

    // Process the last section if any
    if (section.name[0] != '\0') {
        printf("Processing section '%s' at line %zu\n", section.name, section.lnum);

        if (process_section(&section, config) < 0) {
            free_section_entries(&section);
            return -1;
        }
        free_section_entries(&section);
        memset(&section, 0, sizeof(section));
    }

    // Second Pass: Populate each layer
    printf("Starting second pass...\n");

    ln = 0;
    char section_name[MAX_SECTION_NAME_LEN + 1];
    section_name[0] = '\0';

    for (size_t i = 0; config_lines[i] != NULL; i++) {
        ln++;
        char line_buffer[MAX_LINE_LEN + 1];
        strncpy(line_buffer, config_lines[i], MAX_LINE_LEN);
        line_buffer[MAX_LINE_LEN] = '\0';  // Ensure null termination
        char *line = line_buffer;

        // Remove leading whitespace
        while (isspace((unsigned char)*line))
            line++;

        size_t len = strlen(line);

        // Remove trailing whitespace
        while (len > 0 && isspace((unsigned char)line[len - 1]))
            len--;

        if (len == 0)
            continue; // Empty line

        line[len] = '\0'; // Null-terminate the trimmed line

        printf("Line %zu: '%s'\n", ln, line);

        if (line[0] == '#')
            continue; // Comment line

        if (line[0] == '[') {
            // Check if it's a valid section header
            if (len > 2 && line[len - 1] == ']') {
                line[len - 1] = '\0'; // Remove closing ']'
                snprintf(section_name, sizeof(section_name), "%s", line + 1);
                printf("New section: '%s' at line %zu\n", section_name, ln);
                continue;
            }
        }

        // Process entry
        if (!strcmp(section_name, "ids") ||
            !strcmp(section_name, "aliases") ||
            !strcmp(section_name, "global") ||
            section_name[0] == '\0')
            continue;

        // Parse key-value pair
        char *key = NULL;
        char *val = NULL;
        parse_kvp(line, &key, &val);

        if (!key) {
            printf("Error parsing key at line %zu\n", ln);
            return -1;
        }

        char layername[MAX_SECTION_NAME_LEN + 1];
        strncpy(layername, section_name, sizeof(layername) - 1);
        layername[sizeof(layername) - 1] = '\0';

        char *token = strtok(layername, ":");
        if (!token) {
            printf("Error parsing layer name at line %zu\n", ln);
            return -1;
        }

        char entry[MAX_EXP_LEN];
        if (val)
            snprintf(entry, sizeof(entry), "%s.%s = %s", token, key, val);
        else
            snprintf(entry, sizeof(entry), "%s.%s", token, key);

        if (config_add_entry(config, entry) < 0) {
            printf("Error adding entry '%s' at line %zu\n", entry, ln);
        } else {
            printf("Added entry '%s'\n", entry);
        }
    }

    printf("Configuration parsing completed successfully.\n");

    return 0;
}

int config_parse(struct config *config)
{
	// char *content = NULL;
    // if (!(content = read_file())) {
    //     printf("no content read\n");
	// 	return -1;
    // } else {
    //     printf("Config\n");
    //     printf("%s\n", content);
    //     printf("\nConfig End\n");
    // }
	memset(config, 0, sizeof *config);
	config_init_(config); // invalid macro here
	// snprintf(config->path, sizeof(config->path), "%s", "\0");
	// printf("befor parse string heap size: %u bytes\n", xPortGetFreeHeapSize());

	// int res = config_parse_string(config, content);
	// free(content);

	int res = config_parse_file(config);
	return res;
}


// int config_check_match(struct config *config, const char *id, uint8_t flags)
// {
// 	size_t i;

// 	for (i = 0; i < config->nr_ids; i++) {
// 		//Prefix match to allow matching <product>:<vendor> for backward compatibility.
// 		if (strstr(id, config->ids[i].id) == id) {
// 			if (config->ids[i].flags & ID_EXCLUDED) {
// 				return 0;
// 			} else if (config->ids[i].flags & flags) {
// 				return 2;
// 			}
// 		}
// 	}

// 	return config->wildcard ? 1 : 0;
// }

int config_get_layer_index(const struct config *config, const char *name)
{
	size_t i;

	if (!name)
		return -1;

	for (i = 0; i < config->nr_layers; i++)
		if (!strcmp(config->layers[i].name, name))
			return i;

	return -1;
}

/*
 * Adds a binding of the form [<layer>.]<key> = <descriptor expression>
 * to the given config.
 */
/*
 * Adds a binding of the form [<layer>.]<key> = <descriptor expression>
 * to the given config.
 */
int config_add_entry(struct config *config, const char *exp)
{
	char *keyname, *descstr, *dot, *paren, *s;
	char *layername = "main";
	struct descriptor d;
	struct layer *layer;
	int idx;

	static char buf[MAX_EXP_LEN];

	if (strlen(exp) >= MAX_EXP_LEN) {
		err("%s exceeds maximum expression length (%d)", exp, MAX_EXP_LEN);
		return -1;
	}

	strcpy(buf, exp);
	s = buf;

	dot = strchr(s, '.');
	paren = strchr(s, '(');

	if (dot && dot != s && (!paren || dot < paren)) {
		layername = s;
		*dot = 0;
		s = dot+1;
	}

	parse_kvp(s, &keyname, &descstr);
	idx = config_get_layer_index(config, layername);

	if (idx == -1) {
		err("%s is not a valid layer", layername);
		return -1;
	}

	layer = &config->layers[idx];

	if (parse_descriptor(descstr, &d, config) < 0) {
		err("failed to parse descriptor");
		return -1;
	}

	return set_layer_entry(config, layer, keyname, &d);
}