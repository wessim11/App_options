/*! \file
 *
 * \brief Check and Execute specific options for current users
 *
 * \author Jazzar Wessim <wjazzar@plugandtel.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
        <depend>app_mixmonitor</depend>
        <depend>mysqlclient</depend>
        <support_level>extended</support_level>
        <defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 000001 $");

#include "asterisk/module.h"
/** Channel Functions **/
#include "asterisk/channel.h"
/** Pbx Functions **/
#include "asterisk/pbx.h"
#include "asterisk/app_options.h"

/*** DOCUMENTATION
        <application name="Options" language="en_US">
                <synopsis>
                        Check and Execute specific options for current users.
                </synopsis>
                <syntax>
                        <parameter name="exten" required="true">
                                <para>Extension Called by the user.</para>
                        </parameter>
                </syntax>
                <description>
                        <para>Check and Execute specific options for current user.</para>
                        <para>Check if monitor is selected then do monitor application if needed.</para>
                </description>
        </application>

        <configInfo name="app_options" language="en_US">
                <configFile name="options.conf">
                        <configObject name="general">
                                <synopsis>Database Credentials used to request for user's parameter</synopsis>
                                <configOption name="hostname" default="127.0.0.1">
                                        <synopsis>The database hostname</synopsis>
                                </configOption>
                                 <configOption name="username" default="dbaser">
                                        <synopsis>The database username</synopsis>
                                </configOption>
                                <configOption name="secret" default="dbpass">
                                         <synopsis>The database secret</synopsis>
                                </configOption>
                                <configOption name="socket" default="/tmp/mysql.sock">
                                        <synopsis>The database socket</synopsis>
                                </configOption>
                                <configOption name="dbname" default="plugandtel">
                                        <synopsis>The database name to connect to</synopsis>
                                </configOption>
                                <configOption name="port" default="3306">
                                        <synopsis>The database port</synopsis>
                                </configOption>
                        </configObject>

                        <configObject name="options">
                                <synopsis>different parameters needed for this module to run</synopsis>
                                <configOption name="dstPath">
                                        <synopsis>The path used where to save recorded calls</synopsis>
                                </configOption>
                                <configOption name="host">
                                        <synopsis>The hostname of the mediaGateway</synopsis>
                                </configOption>
                                <configOption name="extension">
                                        <synopsis>Extension of audio file to save</synopsis>
                                </configOption>
                        </configObject>
                </configFile>
        </configInfo>
 ***/



/*! \brief allocate a database_configuration structure */
static void *dbCredentials_alloc(void) {
    struct database_configuration *dbInfo;
    if (!(dbInfo = ao2_alloc(sizeof(*dbInfo), dbCredentials_destructor))) {
        ast_log(LOG_WARNING, "Memory Error , Allocation of dbCredentials failed!\n");
        return NULL;
    }
    if (ast_string_field_init(dbInfo, 128)) {
        ast_log(LOG_WARNING, "Memory Error , Allocation of dbCredentials failed!\n");
        ao2_ref(dbInfo, -1);
        return NULL;
    }
    return dbInfo;
}

/*! \brief free a database_configuration structure */
static void dbCredentials_destructor(void *obj) {
    struct database_configuration *dbInfo = obj;
    ast_string_field_free_memory(dbInfo);
    return;
}

/*! \brief allocate a option_configuration structure */
static void *option_alloc(void) {
    struct option_configuration *options;
    if (!(options = ao2_alloc(sizeof(*options), option_destructor))) {
        ast_log(LOG_WARNING, "Memory Error , Allocation of options failed!\n");
        return NULL;
    }
    if (ast_string_field_init(options, 250)) {
        ast_log(LOG_WARNING, "Memory Error , Allocation of options failed!\n");
        ao2_ref(options, -1);
        return NULL;
    }
    return options;
}

/*! \brief free an option_configuration structure */
static void option_destructor(void *obj) {
    struct option_configuration *options = obj;
    ast_string_field_free_memory(options);
    return;
}

/*! \brief allocate a global_option structure */
static void *global_option_alloc(void) {
    struct option_global *global_options;
    if (!(global_options = ao2_alloc(sizeof(*global_options), global_option_destructor))) {
        ast_log(LOG_WARNING, "Memory Error , Allocation of global options failed!\n");
        goto error;
    }
    if (!(global_options->dbCredentials = dbCredentials_alloc())) {
        goto error;
    }
    if (!(global_options->options = option_alloc())) {
        goto error;
    }

    return global_options;
    error:
    ao2_cleanup(global_options);
    return NULL;
}

/*! \brief free an global_option structure */
static void global_option_destructor(void *obj) {
    struct option_global *global_option = obj;
    /* Close DB Connection  after checking if connection is still active*/
    if (!mysql_ping(&global_option->dbCredentials->conn))
        mysql_close(&global_option->dbCredentials->conn);
    ao2_cleanup(global_option->dbCredentials);
    ao2_cleanup(global_option->options);
}

/*! \brief Make checks on data and channel names */
static int dataSanityCheck(struct ast_channel *chan, const char *data) {
    if (ast_strlen_zero(data)) {
        ast_log(LOG_WARNING, "No data Has been passed to Option App!\n");
        return 1;
    }

    int dataLength = strlen(data);
    if (dataLength > 25) {
        ast_log(LOG_WARNING,
                "Destination Number has wrong length , Length must be between 9 and 25 but we have been given [%i]\n",
                dataLength);
        return 1;
    }

    if (!strcasecmp("OutgoingSpoolFailed", ast_channel_name(chan))) {
        ast_log(LOG_WARNING, "OutgoingSpoolFailed on channel[%s]!\n", ast_channel_uniqueid(chan));
        return 1;
    }

    if ((!strcasecmp("s", data)) || (!strcasecmp("h", data)) || (!strcasecmp("t", data)) || (!strcasecmp("i", data)) ||
        (!strcasecmp("failed", data))) {
        ast_log(LOG_DEBUG, "Special extension: [%s]\n", data);
        return 1;
    }

    if (ast_strlen_zero(ast_channel_accountcode(chan))) {
        ast_log(LOG_WARNING, "Channel accountCode hasn't been set!\n");
        return 1;
    }

    return 0;
}

/*! \brief It checks for users option <<Trunk ASP>> and alter AccountCode based on callerId
 * In Other Way , The user can modify his accountCode by sending it as a callerID
 * @param chan
 * @return
 * 0 => Success
 * 1 => Failure
 */
static int is_trunked_asp_account(struct ast_channel *chan, struct database_configuration *dbInfo) {
    char queryString[512];
    int numRows = 0;
    MYSQL_RES *myres = NULL;
    MYSQL_ROW myrow;
    const char *accountCode = ast_channel_accountcode(chan);

    sprintf(
            queryString,
            "SELECT options.cidIsAcode, users.TenantID FROM users INNER JOIN options USING(UserID) WHERE users.UserID='%s'",
            accountCode
    );
    myres = MYSQL_query(myres, &numRows, queryString, dbInfo);
    /** Check if there is data or error **/
    if (numRows < 1) {
        mysql_free_result(myres);
        return 1;
    }
    mysql_data_seek(myres, 0);
    myrow = mysql_fetch_row(myres);
    if (atoi(myrow[0]) == 1) { /** Option is Enable for this user **/
        ast_log(LOG_DEBUG, "Option Trunk ASP is enabled for user[%s]\n", accountCode);
        /** Extract callerId **/
        const char *CallerIdNum = S_COR(ast_channel_caller(chan)->id.number.valid,
                                        ast_channel_caller(chan)->id.number.str, "<Unknown>");

        /** Check if callerid is valid to be interpreted as a callerid **/
        if (is_string_digits(CallerIdNum)) {
            ast_log(LOG_WARNING,
                    "Trunk ASP is enabled , CallerId should correspond to an accountCode but instead we got invalid CallerId[%s]\n",
                    CallerIdNum);
            return 1;
        }
        /** Let's find to wich accountid the callerid refers and modify it ! **/
        sprintf(queryString, "SELECT UserID FROM users WHERE (UserID=%s) AND (TenantID=%s)",
                CallerIdNum, myrow[1]);
        myres = MYSQL_query(myres, &numRows, queryString, dbInfo);
        if (numRows < 1) /** No Rows returned or error **/
        {
            ast_log(LOG_WARNING,
                    "User table said that CallerID corresponds to an Accountcode in the Tenant. But there isn't accountcode for %s value on UserID %s.\n",
                    CallerIdNum, ast_channel_accountcode(chan)
            );
            return 1;
        }
        /** Let's change accountId with callerIdNumber number **/
        mysql_data_seek(myres, 0);
        myrow = mysql_fetch_row(myres);
        /** Set new accountCode **/
        ast_channel_accountcode_set(chan, myrow[0]);
        mysql_free_result(myres);
        return 0;
    }

    ast_log(LOG_DEBUG, "Option TrunkAsp is not enabled on accountCode[%s]\n", ast_channel_accountcode(chan));
    mysql_free_result(myres);
    return 0;
}

/*! \brief Check if prefix is bloqued
 * @param chan
 * @param destNumber
 * @param dbInfo
 * @return
 *  1 => prefix bloqued
 *  0 => prefix allowed
 */
static int
is_prefix_bloqued(struct ast_channel *chan, const char *formattedNumber, struct database_configuration *dbInfo) {
    const char *accountCode = ast_channel_accountcode(chan);
    char querystring[512];
    int numRows = 0;
    MYSQL_RES *myres = NULL;
    MYSQL_ROW myrow;
    int groupNumbers = 0;

    /** Now That number has been formated to international number , let's Check for groups **/
    // Check if users belong to a group
    sprintf(querystring, "SELECT count(GUID) FROM group_user WHERE group_user.UserID=%s", accountCode);
    myres = MYSQL_query(myres, &numRows, querystring, dbInfo);
    if (numRows < 0) /** Error on query , Block ! **/
    {
        mysql_free_result(myres);
        return 1;
    } else { /** Got x group assigned to this user **/
        mysql_data_seek(myres, 0);
        myrow = mysql_fetch_row(myres);
        groupNumbers = atoi(myrow[0]);
        if (!groupNumbers) { /** Zero groups assigned to this user **/
            ast_log(LOG_WARNING, "-- %s : UserID %s is not assigned on a group.\n", ast_channel_uniqueid(chan),
                    accountCode);
            mysql_free_result(myres);
            return 1;
        }
        ast_log(LOG_DEBUG, "-- %s : UserID %s is assigned on %i group(s).\n", ast_channel_uniqueid(chan), accountCode,
                groupNumbers);
    }

    /** How Many  groups are not allowed to dial this prefix **/
    sprintf(querystring,
            "SELECT COUNT(DISTINCT(blocked_prefix_group.GroupID)) FROM blocked_prefix_group INNER JOIN group_user USING(GroupID) WHERE (group_user.UserID=%s) AND (SELECT '%s' LIKE BINARY CONCAT(blocked_prefix_group.prefix,'%s'))",
            accountCode, formattedNumber, "%");
    myres = MYSQL_query(myres, &numRows, querystring, dbInfo);
    if (numRows < 0) /** Errors on Query , block Call **/
    {
        mysql_free_result(myres);
        return 1;
    } else if (numRows) /** User belongs to a list of groups , let's count them **/
    {
        mysql_data_seek(myres, 0);
        myrow = mysql_fetch_row(myres);
        if (groupNumbers == atoi(myrow[0])) {
            ast_log(LOG_WARNING,
                    "-- %s : UserID %s is not allowed to dial this prefix (each group have prohibition).\n",
                    ast_channel_uniqueid(chan), accountCode);
            mysql_free_result(myres);
            return 1;
        }
    }

    /** Check for user's prohibitions **/
    sprintf(querystring,
            "SELECT blocked_prefix_user.prefix FROM blocked_prefix_user WHERE (blocked_prefix_user.UserID=%s) AND (SELECT '%s' LIKE BINARY CONCAT(blocked_prefix_user.prefix,'%s'))",
            accountCode, formattedNumber, "%");

    myres = MYSQL_query(myres, &numRows, querystring, dbInfo);
    if (numRows < 0) /** Error on Query , Force Hangyp **/
    {
        mysql_free_result(myres);
        return 1;
    } else if (numRows) {
        mysql_data_seek(myres, 0);
        myrow = mysql_fetch_row(myres);
        ast_log(LOG_WARNING, "-- %s : UserID %s is not allowed to dial this prefix (prohibition with prefix %s).\n",
                ast_channel_uniqueid(chan), accountCode, myrow[0]);
        mysql_free_result(myres);
        return 1;
    }

    mysql_free_result(myres);
    return 0;
}

/**
* Force Call to hangup by absoluteTimeout
* @param channel_name
**/
static int forceHangup(const char *channel_name) {
    ast_log(LOG_DEBUG, "Hangup Channel[%s]\n", channel_name);
    struct ast_channel *c;
    int causecode = 11;

    if (ast_strlen_zero(channel_name)) {
        ast_log(LOG_WARNING, "Invalid Channel Name passed , Impossible to  hangup channel!\n");
        return 0;
    }


    if (!(c = ast_channel_get_by_name(channel_name))) {
        ast_log(LOG_WARNING, "No Such Channel [%s] found  to be hangup up\n", channel_name);
        return 0;
    }

    ast_channel_softhangup_withcause_locked(c, causecode);
    c = ast_channel_unref(c);

    return 0;
}


/*! \brief Check if users call should be recorded or not
 * @param chan
 * @return
 * 1 Success => Call Must Be recorded
 * 0 Failure => Call won be recorded
 */
static int isCallMonitored(struct ast_channel *chan, struct database_configuration *dbInfo) {
    char queryString[512];
    int numRows = 0;
    MYSQL_RES *myres = NULL;
    MYSQL_ROW myrow;
    const char *accountCode = ast_channel_accountcode(chan);
    /** Check if the group is monitored **/
    sprintf(queryString,
            "SELECT COUNT(GUID) FROM group_user INNER JOIN group_agent USING(GroupID) WHERE (group_user.UserID=%s) AND (group_agent.monitored=1);",
            accountCode
    );
    myres = MYSQL_query(myres, &numRows, queryString, dbInfo);
    if (numRows > -1 && numRows) /** No errors on Query and query returned 1 row **/
    {
        /** If monitor option for group is set to 1 , force recording **/
        mysql_data_seek(myres, 0);
        myrow = mysql_fetch_row(myres);
        if (atoi(myrow[0]) > 0) /** Option monitor group is enabled **/
        {
            ast_log(LOG_DEBUG, "UserID[%s] has group monitoring set to 1\n", accountCode);
            mysql_free_result(myres);
            return 1;
        } else /** Let's Check if the users has recording option set to 1 **/
        {
            sprintf(queryString, "SELECT options.Monitored FROM options WHERE (options.UserId=%s);", accountCode);
            myres = MYSQL_query(myres, &numRows, queryString, dbInfo);
            if (numRows > -1 && numRows) /** No errors on Query  and query returned one row**/
            {
                mysql_data_seek(myres, 0);
                myrow = mysql_fetch_row(myres);
                if (atoi(myrow[0]) > 0) /** User monitoring is enabled **/
                {
                    ast_log(LOG_DEBUG, "UserID[%s] has calls monitoring options set to 1\n", accountCode);
                    mysql_free_result(myres);
                    return 1;
                }
            }
        }
    }

    return 0;
}

/*! \brief Start call recording on this channel */
static void recordCall(struct ast_channel *chan, struct option_configuration *conf) {
    char dateTime[128];
    get_formated_time_now(dateTime);
    struct ast_app *application;
    char application_data[128];
    const char *uniqueid = ast_channel_uniqueid(chan);
    application = pbx_findapp("MixMonitor");
    if (!application) {
        ast_log(LOG_WARNING, "Can't find MixMonitor application,Let's try Monitor Application!\n");
        application = pbx_findapp("Monitor");
        if (!application) {
            ast_log(LOG_WARNING, "Neither MixMonitor|Monitor application were found , This Call won't be recorded!\n");
            return;
        }
        sprintf(application_data, "wav49|%s-%s|m", conf->host, uniqueid);
    } else {
        /** Use MixMonitor **/
        sprintf(application_data, "%s/%s-%s.%s,b,",
                conf->dstPath, uniqueid, dateTime, conf->extension
        );
    }
    /** Exec MixMonitor|Monitor application  **/
    ast_log(LOG_DEBUG, "Monitoring Call on channel with uniqid[%s] and app_data [%s]\n", uniqueid, application_data);
    pbx_exec(chan, application, (void *) application_data);
}

static void
get_international_number(const char *destNumber, char *formattedNumber, struct database_configuration *dbInfo) {
    char querystring[512];
    MYSQL_RES *myres = NULL;
    MYSQL_ROW myrow;
    int numRows;
    char buffer[26];

    sprintf(querystring,
            "SELECT prefix_in.digit_delete, prefix_in.new_prefix FROM prefix_in WHERE ((SELECT '%s' LIKE BINARY CONCAT(prefix_in.prefix,'%s') ) AND (prefix_in.TenantID=1)) ORDER BY CHAR_LENGTH(prefix_in.prefix) DESC LIMIT 1",
            destNumber, "%"
    );
    myres = MYSQL_query(myres, &numRows, querystring, dbInfo); /** Let's try with another request to DB **/
    if (numRows < 0) {
        sprintf(formattedNumber, "%s", destNumber);
        mysql_free_result(myres);
        return;
    }

    if (numRows) /** Data returned **/
    {
        mysql_data_seek(myres, 0);
        myrow = mysql_fetch_row(myres);
        /** copy from i position , (sizeBuffer - number discarded) to buffer **/
        strncpy(buffer, destNumber + atoi(myrow[0]), 26 - atoi(myrow[0]));
        sprintf(formattedNumber, "%s%s", myrow[1], buffer);
        ast_log(LOG_DEBUG, "-- International number is %s.\n", formattedNumber);
    } else {
        sprintf(formattedNumber, "%s", destNumber);
        ast_log(LOG_DEBUG, "-- International number is %s.\n", formattedNumber);
    }
}

/*! \brief Check if Dynamic display of numbers is enabled **/
static int isRcliOnCountryEnabled(struct ast_channel *chan, struct database_configuration *dbInfo) {
    char queryString[512];
    MYSQL_RES *myres = NULL;
    MYSQL_ROW myrow;
    int numRows;
    const char *accountCode = ast_channel_accountcode(chan);//UserID

    sprintf(queryString,
            "SELECT options.RCLI, users.TenantID FROM users INNER JOIN options USING(UserID) WHERE users.UserID='%s'",
            accountCode
    );

    myres = MYSQL_query(myres, &numRows, queryString, dbInfo);
    /** Check if there is data or error **/
    if (numRows < 1) {
        mysql_free_result(myres);
        return 0;
    }
    mysql_data_seek(myres, 0);
    myrow = mysql_fetch_row(myres);

    if (atoi(myrow[0])) {
        ast_log(LOG_DEBUG, "User[%s] has RcliOnCountry Enabled!\n", accountCode);
        return 1;
    }

    return 0;
}

/** Start RcliOnCountry logic **/
static void startRcliOnCountry(struct ast_channel *chan, const char *formattedNumber, struct database_configuration *dbInfo) {
    const char* accountCode = ast_channel_accountcode(chan);
    char queryString[512];
    MYSQL_RES *myres = NULL;
    MYSQL_ROW myrow;
    int numRows;
    int prefix = 0;
    


    if( !strncmp(formattedNumber , "33" , 2)){
        prefix = formattedNumber[2] - '0' ;
        ast_log(LOG_DEBUG, "French Number Detected[%s] and prefix is %d\n", formattedNumber, prefix);
        /** Let's search for all Sda that belongs to this prefix **/
        sprintf(queryString , "select did from dids NATURAL JOIN didToUser WHERe didToUser.userid = %s AND dids.did LIKE '0%d%%'"
                , accountCode , prefix );
        /** Let's Query **/
        myres = MYSQL_query(myres , &numRows , queryString , dbInfo);
        if(numRows < 1 ){
            ast_log(LOG_WARNING , "RcliOnCountry is Enabled but user[%s] have no Sda assigned for prefix[0%d]\n" , accountCode , prefix);
            mysql_free_result(myres);
            return ;
        }

        ast_log(LOG_DEBUG , "User[%s] has %d sda assigned to it\n" , accountCode , numRows);
        //Let's rand for a random number
        time_t t;
        srand((unsigned)time(&t));
        int sdaToChoosePrefix = rand() % numRows ;
        mysql_data_seek(myres, sdaToChoosePrefix);
        myrow = mysql_fetch_row(myres);
        /** We Got Our Sda , Let's modify it **/
        ast_log(LOG_DEBUG , "Number[%s] has been chosen\n" , myrow[0]);
        ast_channel_caller(chan)->id.number.str = ast_strdup(myrow[0]);
        ast_channel_caller(chan)->id.name.str = ast_strdup(myrow[0]);
        mysql_free_result(myres);
    } else {
        ast_log(LOG_DEBUG, "RcliOnCountry Enabled but destnumber[%s] is not a french destination\n", formattedNumber);
        return;
    }

}


/*! \brief main function , executed everytime our application is executed */
static int app_exec(struct ast_channel *chan, const char *data) {
    char formattedNumber[26];
    if (dataSanityCheck(chan, data)) {
        ast_log(LOG_DEBUG, "Sanity Check Has failed [ABORTING]!\n");
        return -1;
    }
    /** Get global Configuration **/
    RAII_VAR(struct option_global *, cfg, ao2_global_obj_ref(options_globals), ao2_cleanup);
    /** Format Number to international number **/
    get_international_number(data, formattedNumber, cfg->dbCredentials);
    /** Check for option trunkASP **/
    is_trunked_asp_account(chan, cfg->dbCredentials);
    /** Check if prefix is bloqued **/
    if (is_prefix_bloqued(chan, formattedNumber, cfg->dbCredentials))
        forceHangup(ast_channel_name(chan));
    /** Check if Call should be monitored/recorded in our case **/
    if (isCallMonitored(chan, cfg->dbCredentials))
        recordCall(chan, cfg->options);
    /** Check if Option RcliOnCountry is enabled **/
    if (isRcliOnCountryEnabled(chan, cfg->dbCredentials))
        startRcliOnCountry(chan, formattedNumber, cfg->dbCredentials);

    return 0;
}


/*! \internal \brief reload handler
 * \retval AST_MODULE_LOAD_SUCCESS on success
 * \retval AST_MODULE_LOAD_DECLINE on failure
 */
static int reload_module(void) {
    ast_unregister_application(app);
    if (aco_process_config(&cfg_info, 1) || ast_register_application_xml(app, app_exec)) {
        ast_log(LOG_WARNING, "Error While reloading application %s\n", app);
        return AST_MODULE_LOAD_DECLINE;
    }
    return AST_MODULE_LOAD_SUCCESS;
}

/*! \internal \brief unload handler */
static int unload_module(void) {
    ast_unregister_application(app);
    aco_info_destroy(&cfg_info);
    return 0;
}

/*! \internal \brief load handler
 * \retval AST_MODULE_LOAD_SUCCESS on success
 * \retval AST_MODULE_LOAD_DECLINE on failure
 */
static int load_module(void) {
    /** Register Our application **/
    if (loadConfiguration() || ast_register_application_xml(app, app_exec)) {
        ast_log(LOG_WARNING, "Error While loading application %s\n", app);
        return AST_MODULE_LOAD_DECLINE;
    }
    RAII_VAR(struct option_global *, cfg, ao2_global_obj_ref(options_globals), ao2_cleanup);
#ifdef DEBUG_OPTIONS
    displayConfiguration(cfg);
#endif
    /** Connect to DB **/
    if (MYSQL_connect(cfg->dbCredentials)) {
        ast_log(LOG_WARNING, "Error While connecting to Mysql database\n");
        unload_module();
        return AST_MODULE_LOAD_DECLINE;
    };
    ast_verb(0, "  == Database Connection : Successfull\n");

    return AST_MODULE_LOAD_SUCCESS;
}


/*! \internal \brief load configuration of this module from config file */
static int loadConfiguration() {
    /** Load Configuration options **/
    if (aco_info_init(&cfg_info)) {
        goto load_error;
    }


    aco_option_register(&cfg_info,
                        "hostname",                                /* Extract configuration item "hostname" */
                        ACO_EXACT,                                 /* Match the exact configuration item name */
                        dbCredentials_mappings,                         /* Use the databasep_options array to find the object to populate */
                        "127.0.0.1",                               /* supply a default value */
                        OPT_STRINGFIELD_T,                          /* Interpret the value as a character array */
                        0,                                         /* No interpretation flags are needed */
                        STRFLDSET(
                                struct database_configuration, hostname)); /* Store the value in member hostname of a database_options struct */

    aco_option_register(&cfg_info, "username",                      /* Extract configuration item "username" */
                        ACO_EXACT,                                  /* Match the exact configuration item name */
                        dbCredentials_mappings,                          /* Use the databasep_options array to find the object to populate */
                        "dbaser",                                   /* supply a default value */
                        OPT_STRINGFIELD_T,                           /* Interpret the value as a character array */
                        0,                                          /* No interpretation flags are needed */
                        STRFLDSET(
                                struct database_configuration, username )); /* Store the value in member username of a database_configuration struct */

    aco_option_register(&cfg_info, "secret",                        /* Extract configuration item "secret" */
                        ACO_EXACT,                                  /* Match the exact configuration item name */
                        dbCredentials_mappings,                          /* Use the databasep_options array to find the object to populate */
                        "dbpass",                                   /* supply a default value */
                        OPT_STRINGFIELD_T,                           /* Interpret the value as a character array */
                        0,                                          /* No interpretation flags are needed */
                        STRFLDSET(
                                struct database_configuration, secret ));  /* Store the value in member secret of database_configuration struct */

    aco_option_register(&cfg_info, "dbname",                       /* Extract configuration item "dbname" */
                        ACO_EXACT,                                 /* Match the exact configuration item name */
                        dbCredentials_mappings,                         /* Use the databasep_options array to find the object to populate */
                        "plugandtel",                              /* supply a default value */
                        OPT_STRINGFIELD_T,                          /* Interpret the value as a character array */
                        0,                                         /* No interpretation flags are needed */
                        STRFLDSET(
                                struct database_configuration, dbname )); /* Store the value in member dbname of a database_configuration struct */


    aco_option_register(&cfg_info, "socket",                        /* Extract configuration item "socket" */
                        ACO_EXACT,                                  /* Match the exact configuration item name */
                        dbCredentials_mappings,                          /* Use the databasep_options array to find the object to populate */
                        "/tmp/mysql.sock",                          /* supply a default value */
                        OPT_STRINGFIELD_T,                           /* Interpret the value as a character array */
                        0,                                          /* No interpretation flags are needed */
                        STRFLDSET(
                                struct database_configuration, socket )); /* Store the value in member socket of a database_configuration struct */


    aco_option_register(&cfg_info, "dstPath",                        /* Extract configuration item "dstPath" */
                        ACO_EXACT,                                   /* Match the exact configuration item name */
                        options_mappings,                             /* Use the configp_options array to find the object to populate */
                        NULL,                                        /* Don't supply a default value */
                        OPT_STRINGFIELD_T,                            /* Interpret the value as a character array */
                        0,                                           /* No interpretation flags are needed */
                        STRFLDSET(
                                struct option_configuration, dstPath)); /* Store the value in member dstPath of option_configuration struct */

    aco_option_register(&cfg_info, "host",                           /* Extract configuration item "host" */
                        ACO_EXACT,                                   /* Match the exact configuration item name */
                        options_mappings,                            /* Use the configp_options array to find the object to populate */
                        "LEA-DEFAULT",                               /* Don't supply a default value */
                        OPT_STRINGFIELD_T,                            /* Interpret the value as a character array */
                        0,                                           /* No interpretation flags are needed */
                        STRFLDSET(
                                struct option_configuration, host)); /* Store the value in member dstPath of option_configuration struct */

    aco_option_register(&cfg_info, "extension",                      /* Extract configuration item "host" */
                        ACO_EXACT,                                   /* Match the exact configuration item name */
                        options_mappings,                            /* Use the configp_options array to find the object to populate */
                        "WAV",                                       /* Don't supply a default value */
                        OPT_STRINGFIELD_T,                           /* Interpret the value as a character array */
                        0,                                           /* No interpretation flags are needed */
                        STRFLDSET(
                                struct option_configuration, extension)); /* Store the value in member dstPath of option_configuration struct */

    aco_option_register(&cfg_info, "port",                           /* Extract configuration item "port" */
                        ACO_EXACT,                                   /* Match the exact configuration item name */
                        dbCredentials_mappings,                           /* Use the general_options array to find the object to populate */
                        "3306",                                      /* supply a default value */
                        OPT_INT_T,                                   /* Interpret the value as an integer */
                        PARSE_IN_RANGE,                              /* Accept values in a range */
                        FLDSET(
                                struct database_configuration, port),      /* Store the value in member port of a database_configuration struct */
                        0,                                                 /* Use MIN as the minimum value of the allowed range */
                        20000);                                            /* Use MAX as the maximum value of the allowed range */



    if (aco_process_config(&cfg_info, 0)) {
        goto load_error;
    }

    //Success
    return 0;

    load_error:
    ast_log(LOG_ERROR, "Error While Loading Configuration file [%s] --> ABORTING!\n", app_configfile);
    aco_info_destroy(&cfg_info);
    return -1;
}

/*! \brief Display Configuration saved from config file for this module */
static void displayConfiguration(struct option_global *cfg) {

    if (!cfg || !cfg->dbCredentials || !cfg->options) {
        ast_log(LOG_ERROR, "Rut roh - something blew away our configuration!");
        return;
    }

    ast_verb(0, "  == Database Configuration:\n"
            "\t[DbCredentials]->hostname = [%s]\n"
            "\t[DbCredentials]->username = [%s]\n"
            "\t[DbCredentials]->secret   = [%s]\n"
            "\t[DbCredentials]->dbname   = [%s]\n"
            "\t[DbCredentials]->socket   = [%s]\n"
            "\t[DbCredentials]->port     = [%d]\n"
            "  == Options Configuration:\n"
            "\t[Options]->dstPath        = [%s]\n"
            "\t[Options]->host           = [%s]\n"
            "\t[Options]->extension      = [%s]\n",
             cfg->dbCredentials->hostname, cfg->dbCredentials->username, cfg->dbCredentials->secret,
             cfg->dbCredentials->dbname, cfg->dbCredentials->socket,
             cfg->dbCredentials->port, cfg->options->dstPath, cfg->options->host, cfg->options->extension
    );
}


/*! \brief Connect to Mysql using database_configuraiton access */
int MYSQL_connect(struct database_configuration *dbInfo) {
    my_bool reconnect = 1;
    mysql_init(&dbInfo->conn);
    mysql_options(&dbInfo->conn, MYSQL_OPT_RECONNECT, &reconnect);
    if (&dbInfo->conn) {
        if (mysql_real_connect(&dbInfo->conn, dbInfo->hostname, dbInfo->username, dbInfo->secret, dbInfo->dbname,
                               (unsigned int) dbInfo->port, dbInfo->socket, 0)) {
            return 0;
        } else {
            ast_log(LOG_WARNING, "mysql_real_connect(mysql,%s,%s,*****,%s,....) failed\n",
                    dbInfo->hostname, dbInfo->username, dbInfo->dbname
            );
        }
    } else {
        ast_log(LOG_WARNING, "mysql_init function returned NULL\n");
    }

    return 1;
}

/*! \brief Connect to Mysql using database_configuration access */
MYSQL_RES *MYSQL_query(MYSQL_RES *mysqlRes, int *numRows, char *querystring, struct database_configuration *dbInfo) {
    ast_log(LOG_DEBUG, "--Query:[%s]\n", querystring);
    mysql_free_result(mysqlRes);
    mysql_real_query(&dbInfo->conn, querystring, strlen(querystring));
    /** Check For Errors **/
    if (mysql_errno(&dbInfo->conn)) {
        ast_log(LOG_ERROR, "Mysql return an Error (%i) : %s on MySQL query:\n[%s]\n",
                mysql_errno(&dbInfo->conn), mysql_error(&dbInfo->conn), querystring
        );
        *numRows = -1;
        return NULL;
    }
    /** Check For Results **/
    mysqlRes = mysql_store_result(&dbInfo->conn);
    if (mysqlRes) {
        *numRows = (int) mysql_num_rows(mysqlRes);
        return mysqlRes;
    } else { /** 0 Rows Returned **/
        *numRows = 0;
        return NULL;
    }
}

/*! \brief Check if string contains only digits
 *  \returns
 *  0 => success
 *  1 => failure
 */
static int is_string_digits(const char *data) {
    if (ast_strlen_zero(data)) {
        ast_log(LOG_DEBUG, "Data Passed was empty!\n");
        return 1;
    }
    int i = 0;
    while (i < strlen(data)) {
        if (!isdigit(data[i])) {
            return 1;
        }
        i++;
    }

    return 0;
}


/*! \brief Save DateTime in destTIme as a string time suiting th format of DATE_FORMAT
 * @param destTime
 * @return
 */
static int get_formated_time_now(char *destTime) {
    time_t rawtime;
    struct tm *info;
    /** Get Time **/
    time(&rawtime);
    info = localtime(&rawtime);


    strftime(destTime, 128, DATE_FORMAT, info);

    return 0;
}


AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Check and Execute specific options for current user",
                .load = load_module,
                .unload = unload_module,
                .reload = reload_module,
                .load_pri = AST_MODPRI_DEFAULT,
);

