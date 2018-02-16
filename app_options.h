/*! \file
 *
 * \brief Check and Execute specific options for current users
 *
 * \author Jazzar Wessim <wjazzar@plugandtel.com>
 *
 * \ingroup applications
 */

#ifndef APP_OPTIONS_APP_OPTIONS_H
#define APP_OPTIONS_APP_OPTIONS_H

#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "mysql.h"


#define DEBUG_OPTIONS 1
#define DATE_FORMAT "%Y%m%d-%H%M%S"



/**
 * Static variables
 */
static const char app[] = "Options";
static const char app_configfile[] = "options.conf";


/**
 * Structures
 */

/*! \brief database_configuration parameter structure
 */
struct database_configuration {
    AST_DECLARE_STRING_FIELDS(
            AST_STRING_FIELD(hostname);
            AST_STRING_FIELD(username);
            AST_STRING_FIELD(secret);
            AST_STRING_FIELD(dbname);
            AST_STRING_FIELD(socket);
    );
    MYSQL conn ;
    int port;
};

/*! \brief option_configuration parameters structure
*/
struct option_configuration {
    AST_DECLARE_STRING_FIELDS(
            AST_STRING_FIELD(dstPath);
            AST_STRING_FIELD(host);
            AST_STRING_FIELD(extension);
    );
};

/*! \brief All configuration objects for this module
 */
struct option_global {
    struct database_configuration *dbCredentials;                           /*< Our global database settings */
    struct option_configuration *options;                                   /*< Our options configuration    */
};

/*! \brief A container that holds our global module options configuration */
static AO2_GLOBAL_OBJ_STATIC(options_globals);

/*! \brief A mapping of the database_configuration struct's general settings to the context
 *         in the configuration file that will populate its values */
static struct aco_type dbCredentials_mapping = {
        .name = "general",
        .type = ACO_GLOBAL,
        .item_offset = offsetof(struct option_global, dbCredentials),
        .category = "^general$",
        .category_match = ACO_WHITELIST
};

struct aco_type *dbCredentials_mappings[] = ACO_TYPES(&dbCredentials_mapping);

/*! \brief A mapping of the module_config struct's option settings to the context
 *         in the configuration file that will populate its values */
static struct aco_type options_mapping = {
        .name = "options",
        .type = ACO_GLOBAL,
        .item_offset = offsetof(struct option_global, options),
        .category = "^options$",
        .category_match = ACO_WHITELIST
};

struct aco_type *options_mappings[] = ACO_TYPES(&options_mapping);

static struct aco_file module_conf = {
        .filename = "options.conf",                              /*!< The name of the config file */
        .types = ACO_TYPES(&dbCredentials_mapping, &options_mapping)   /*!< The mapping object types to be processed */
};


/**
 * Prototypes
 */
static void displayConfiguration(struct option_global*);

static void *dbCredentials_alloc(void);

static void dbCredentials_destructor(void *obj);

static void *option_alloc(void);

static void option_destructor(void *obj);

static void *global_option_alloc(void);

static void global_option_destructor(void *obj);

static int loadConfiguration(void);

int MYSQL_connect(struct database_configuration* dbInfo);

MYSQL_RES *MYSQL_query(MYSQL_RES *,int *, char*, struct database_configuration*);

static int dataSanityCheck(struct ast_channel* chan ,const char* data);

static int is_trunked_asp_account(struct ast_channel *chan, struct database_configuration *dbInfo);

static int isCallMonitored(struct ast_channel *chan, struct database_configuration *dbInfo);

static int is_prefix_bloqued(struct ast_channel* chan , const char* formattedNumber , struct database_configuration* dbInfo);

static int forceHangup(const char*  channel_name );

static void recordCall(struct ast_channel* chan , struct option_configuration* conf);

static int is_string_digits(const char* data);

static int get_formated_time_now(char *destTime);

static int isRcliOnCountryEnabled(struct ast_channel* chan , struct database_configuration* dbInfo);

static void startRcliOnCountry(struct ast_channel* chan , const char* formattedNumber , struct database_configuration* dbInfo);


CONFIG_INFO_STANDARD(cfg_info, options_globals, global_option_alloc,
                     .files = ACO_FILES(&module_conf)
);


#endif //APP_OPTIONS_APP_OPTIONS_H

