#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>

#include <cups/cups.h>

#include "cups-notifier.h"

#include <cpdb/backend.h>
#include "backend_helper.h"

#define _CUPS_NO_DEPRECATED 1
#define BUS_NAME "org.openprinting.Backend.CUPS"

static void
on_name_acquired(GDBusConnection *connection,
                 const gchar *name,
                 gpointer not_used);
static void acquire_session_bus_name();
gpointer list_printers(gpointer _dialog_name);
int send_printer_added(void *_dialog_name, unsigned flags, cups_dest_t *dest);
void connect_to_signals();

BackendObj *b;

void update_printer_lists()
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, b->dialogs);
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        char *dialog_name = key;
        refresh_printer_list(b, dialog_name);
    }
}

static void
on_printer_state_changed (CupsNotifier *object,
                          const gchar *text,
                          const gchar *printer_uri,
                          const gchar *printer,
                          guint printer_state,
                          const gchar *printer_state_reasons,
                          gboolean printer_is_accepting_jobs,
                          gpointer user_data)
{
    loginfo("Printer state change on printer %s: %s\n", printer, text);
    
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, b->dialogs);
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        const char *dialog_name = key;
        PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer);
        const char *state = get_printer_state(p);
        send_printer_state_changed_signal(b, dialog_name, printer,
                                            state, printer_is_accepting_jobs);
    }
}

static void
on_printer_added (CupsNotifier *object,
                  const gchar *text,
                  const gchar *printer_uri,
                  const gchar *printer,
                  guint printer_state,
                  const gchar *printer_state_reasons,
                  gboolean printer_is_accepting_jobs,
                  gpointer user_data)
{
    loginfo("Printer added: %s\n", text);
    update_printer_lists();
}

static void
on_printer_deleted (CupsNotifier *object,
                    const gchar *text,
                    const gchar *printer_uri,
                    const gchar *printer,
                    guint printer_state,
                    const gchar *printer_state_reasons,
                    gboolean printer_is_accepting_jobs,
                    gpointer user_data)
{
    loginfo("Printer deleted: %s\n", text);
    update_printer_lists();
}

int main()
{
    /* Initialize internal default settings of the CUPS library */
    int p = ippPort();

    b = get_new_BackendObj();
    cpdbInit();
    acquire_session_bus_name(BUS_NAME);

    int subscription_id = create_subscription();

    g_timeout_add_seconds (NOTIFY_LEASE_DURATION - 60,
                            renew_subscription_timeout,
                            &subscription_id);

    GError *error = NULL;
    CupsNotifier *cups_notifier = cups_notifier_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
                                                                       0,
                                                                       NULL,
                                                                       CUPS_DBUS_PATH,
                                                                       NULL,
                                                                       &error);
    if (error)
    {
        logwarn("Error creating cups notify handler: %s", error->message);
        g_error_free(error);
        cups_notifier = NULL;
    }

    if (cups_notifier != NULL)
    {
        g_signal_connect(cups_notifier, "printer-state-changed",
                            G_CALLBACK(on_printer_deleted), NULL);
        g_signal_connect(cups_notifier, "printer-deleted",
                            G_CALLBACK(on_printer_deleted), NULL);
        g_signal_connect(cups_notifier, "printer-added",
                            G_CALLBACK(on_printer_added), NULL);
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* Main loop exited */
    logdebug("Main loop exited");
    g_main_loop_unref(loop);
    loop = NULL;

    cancel_subscription(subscription_id);
    if (cups_notifier)
        g_object_unref(cups_notifier);
}

static void acquire_session_bus_name(char *bus_name)
{
    g_bus_own_name(G_BUS_TYPE_SESSION,
                   bus_name,
                   0,                //flags
                   NULL,             //bus_acquired_handler
                   on_name_acquired, //name acquired handler
                   NULL,             //name_lost handler
                   NULL,             //user_data
                   NULL);            //user_data free function
}

static void
on_name_acquired(GDBusConnection *connection,
                 const gchar *name,
                 gpointer not_used)
{
    b->dbus_connection = connection;
    b->skeleton = print_backend_skeleton_new();
    connect_to_signals();
    connect_to_dbus(b, CPDB_BACKEND_OBJ_PATH);
}

static gboolean on_handle_get_printer_list(PrintBackend *interface,
                                           GDBusMethodInvocation *invocation,
                                           gpointer user_data)
{
    int num_printers;
    GHashTableIter iter;
    gpointer key, value;
    GVariantBuilder builder;
    GVariant *printer, *printers;

    cups_dest_t *dest;
    gboolean accepting_jobs;
    const char *state;
    char *name, *info, *location, *make;

    GHashTable *table = cups_get_all_printers();
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);

    add_frontend(b, dialog_name);
    num_printers = g_hash_table_size(table);
    if (num_printers == 0)
    {
        printers = g_variant_new_array(G_VARIANT_TYPE ("(v)"), NULL, 0);
        print_backend_complete_get_printer_list(interface, invocation, 0, printers);
        return TRUE;
    }

    g_hash_table_iter_init(&iter, table);
    g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        name = key;
        dest = value;
        loginfo("Found printer : %s\n", name);
        info = cups_retrieve_string(dest, "printer-info");
        location = cups_retrieve_string(dest, "printer-location");
        make = cups_retrieve_string(dest, "printer-make-and-model");
        accepting_jobs = cups_is_accepting_jobs(dest);
        state = cups_printer_state(dest);
        add_printer_to_dialog(b, dialog_name, dest);
        printer = g_variant_new(CPDB_PRINTER_ARGS, dest->name, dest->name, info,
                                location, make, accepting_jobs, state, BACKEND_NAME);
        g_variant_builder_add(&builder, "(v)", printer);
        free(key);
        cupsFreeDests(1, value);
        free(info);
        free(location);
        free(make);
    }
    g_hash_table_destroy(table);
    printers = g_variant_builder_end(&builder);

    print_backend_complete_get_printer_list(interface, invocation, num_printers, printers);
    return TRUE;
}

static gboolean on_handle_get_all_translations(PrintBackend *interface,
                                               GDBusMethodInvocation *invocation,
                                               const gchar *printer_name,
                                               const gchar *locale,
                                               gpointer user_data)
{
    PrinterCUPS *p;
    GVariant *translations;
    const char *dialog_name;

    dialog_name = g_dbus_method_invocation_get_sender(invocation);
    p = get_printer_by_name(b, dialog_name, printer_name);
    translations = get_printer_translations(p, locale);
    print_backend_complete_get_all_translations(interface, invocation, translations);

    return TRUE;
}

gpointer list_printers(gpointer _dialog_name)
{
    char *dialog_name = (char *)_dialog_name;
    g_message("New thread for dialog at %s\n", dialog_name);
    int *cancel = get_dialog_cancel(b, dialog_name);

    cupsEnumDests(CUPS_DEST_FLAGS_NONE,
                  -1, //NO timeout
                  cancel,
                  0, //TYPE
                  0, //MASK
                  send_printer_added,
                  _dialog_name);

    g_message("Exiting thread for dialog at %s\n", dialog_name);
    return NULL;
}

int send_printer_added(void *_dialog_name, unsigned flags, cups_dest_t *dest)
{

    char *dialog_name = (char *)_dialog_name;
    char *printer_name = dest->name;

    if (dialog_contains_printer(b, dialog_name, printer_name))
    {
        g_message("%s already sent.\n", printer_name);
        return 1;
    }

    add_printer_to_dialog(b, dialog_name, dest);
    send_printer_added_signal(b, dialog_name, dest);
    g_message("     Sent notification for printer %s\n", printer_name);

    /** dest will be automatically freed later. 
     * Don't explicitly free it
     */
    return 1; //continue enumeration
}

static void on_stop_backend(GDBusConnection *connection,
                            const gchar *sender_name,
                            const gchar *object_path,
                            const gchar *interface_name,
                            const gchar *signal_name,
                            GVariant *parameters,
                            gpointer not_used)
{
    g_message("Stop backend signal from %s\n", sender_name);
    /**
     * Ignore this signal if the dialog's keep_alive variable is set
     */
    Dialog *d = find_dialog(b, sender_name);
    if (d && d->keep_alive)
        return;

    set_dialog_cancel(b, sender_name);
    remove_frontend(b, sender_name);
    if (no_frontends(b))
    {
        g_message("No frontends connected .. exiting backend.\n");
        exit(EXIT_SUCCESS);
    }
}

static void on_hide_remote_printers(GDBusConnection *connection,
                                    const gchar *sender_name,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *signal_name,
                                    GVariant *parameters,
                                    gpointer not_used)
{
    char *dialog_name = cpdbGetStringCopy(sender_name);
    g_message("%s signal from %s\n", CPDB_SIGNAL_HIDE_REMOTE, dialog_name);
    if (!get_hide_remote(b, dialog_name))
    {
        set_dialog_cancel(b, dialog_name);
        set_hide_remote_printers(b, dialog_name);
        refresh_printer_list(b, dialog_name);
    }
}

static void on_unhide_remote_printers(GDBusConnection *connection,
                                      const gchar *sender_name,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer not_used)
{
    char *dialog_name = cpdbGetStringCopy(sender_name);
    g_message("%s signal from %s\n", CPDB_SIGNAL_UNHIDE_REMOTE, dialog_name);
    if (get_hide_remote(b, dialog_name))
    {
        set_dialog_cancel(b, dialog_name);
        unset_hide_remote_printers(b, dialog_name);
        refresh_printer_list(b, dialog_name);
    }
}

static void on_hide_temp_printers(GDBusConnection *connection,
                                  const gchar *sender_name,
                                  const gchar *object_path,
                                  const gchar *interface_name,
                                  const gchar *signal_name,
                                  GVariant *parameters,
                                  gpointer not_used)
{
    char *dialog_name = cpdbGetStringCopy(sender_name);
    g_message("%s signal from %s\n", CPDB_SIGNAL_HIDE_TEMP, dialog_name);
    if (!get_hide_temp(b, dialog_name))
    {
        set_dialog_cancel(b, dialog_name);
        set_hide_temp_printers(b, dialog_name);
        refresh_printer_list(b, dialog_name);
    }
}

static void on_unhide_temp_printers(GDBusConnection *connection,
                                    const gchar *sender_name,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *signal_name,
                                    GVariant *parameters,
                                    gpointer not_used)
{
    char *dialog_name = cpdbGetStringCopy(sender_name);
    g_message("%s signal from %s\n", CPDB_SIGNAL_UNHIDE_TEMP, dialog_name);
    if (get_hide_temp(b, dialog_name))
    {
        set_dialog_cancel(b, dialog_name);
        unset_hide_temp_printers(b, dialog_name);
        refresh_printer_list(b, dialog_name);
    }
}

static gboolean on_handle_is_accepting_jobs(PrintBackend *interface,
                                            GDBusMethodInvocation *invocation,
                                            const gchar *printer_name,
                                            gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    cups_dest_t *dest = get_dest_by_name(b, dialog_name, printer_name);
    g_assert_nonnull(dest);
    print_backend_complete_is_accepting_jobs(interface, invocation, cups_is_accepting_jobs(dest));
    return TRUE;
}

static gboolean on_handle_get_printer_state(PrintBackend *interface,
                                            GDBusMethodInvocation *invocation,
                                            const gchar *printer_name,
                                            gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    const char *state = get_printer_state(p);
    printf("%s is %s\n", printer_name, state);
    print_backend_complete_get_printer_state(interface, invocation, state);
    return TRUE;
}

static gboolean on_handle_get_option_translation(PrintBackend *interface,
                                                 GDBusMethodInvocation *invocation,
                                                 const gchar *printer_name,
                                                 const gchar *option_name,
                                                 const gchar *locale,
                                                 gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    char *translation = get_option_translation(p, option_name, locale);
    if (translation == NULL)
        translation = cpdbGetStringCopy(option_name);
    print_backend_complete_get_option_translation(interface, invocation, translation);
    return TRUE;
}

static gboolean on_handle_get_choice_translation(PrintBackend *interface,
                                                 GDBusMethodInvocation *invocation,
                                                 const gchar *printer_name,
                                                 const gchar *option_name,
                                                 const gchar *choice_name,
                                                 const gchar *locale,
                                                 gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    char *translation = get_choice_translation(p, option_name,
                                                choice_name, locale);
    if (translation == NULL)
        translation = cpdbGetStringCopy(choice_name);
    print_backend_complete_get_choice_translation(interface, invocation, translation);
    return TRUE;
}

static gboolean on_handle_get_group_translation(PrintBackend *interface,
                                                GDBusMethodInvocation *invocation,
                                                const gchar *printer_name,
                                                const gchar *group_name,
                                                const gchar *locale,
                                                gpointer user_data)
{
    char *translation = cpdbGetGroupTranslation2(group_name, locale);
    print_backend_complete_get_group_translation(interface, invocation, translation);
    free(translation);
    return TRUE;
}

static gboolean on_handle_ping(PrintBackend *interface,
                               GDBusMethodInvocation *invocation,
                               const gchar *printer_name,
                               gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    print_backend_complete_ping(interface, invocation);
    tryPPD(p);
    return TRUE;
}




static gboolean on_handle_print_file(PrintBackend *interface,
                                     GDBusMethodInvocation *invocation,
                                     const gchar *printer_id,
                                     int num_settings,
                                     GVariant *settings,
                                     gchar **jobid,
                                     gchar **socket,
                                     gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_id);

    // Call the renamed function
    PrintResult *result;
    print_socket(p, num_settings, settings, &result);

    // Set the out parameters for the D-Bus method
    *jobid = result->jobid;
    *socket = result->socket;

    // Complete the D-Bus method call with the result
    print_backend_complete_print_socket(interface, invocation, *jobid, *socket);

    // Handle your cleanup or additional logic here

    if (no_frontends(b))
    {
        g_message("No frontends connected .. exiting backend.\n");
        exit(EXIT_SUCCESS);
    }

    return TRUE;
}






// static gboolean on_handle_print_file(PrintBackend *interface,
//                                      GDBusMethodInvocation *invocation,
//                                      const gchar *printer_name,
//                                      const gchar *file_path,
//                                      int num_settings,
//                                      GVariant *settings,
//                                      const gchar *final_file_path,
//                                      gpointer user_data)
// {
//     const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
//     PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);

//     int job_id = print_file(p, file_path, num_settings, settings);

//     char jobid_string[64];
//     snprintf(jobid_string, sizeof(jobid_string), "%d", job_id);
//     print_backend_complete_print_file(interface, invocation, jobid_string);

//     /**
//      * (Currently Disabled) Printing will always be the last operation, so remove that frontend
//      */
//     //set_dialog_cancel(b, dialog_name);
//     //remove_frontend(b, dialog_name);

//     if (no_frontends(b))
//     {
//         g_message("No frontends connected .. exiting backend.\n");
//         exit(EXIT_SUCCESS);
//     }
//     return TRUE;
// }

static gboolean on_handle_get_all_options(PrintBackend *interface,
                                          GDBusMethodInvocation *invocation,
                                          const gchar *printer_name,
                                          gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    
    Media *medias;
    int media_count = get_all_media(p, &medias);
    GVariantBuilder *builder;
    GVariant *media_variant;
    builder = g_variant_builder_new(G_VARIANT_TYPE("a(siiia(iiii))"));
    
    for (int i = 0; i < media_count; i++)
    {
		GVariant *tuple = pack_media(&medias[i]);
		g_variant_builder_add_value(builder, tuple);
	}
	media_variant = g_variant_builder_end(builder);
    
    Option *options;
    int count = get_all_options(p, &options);
    count = add_media_to_options(p, medias, media_count, &options, count);
    GVariant *variant;
    builder = g_variant_builder_new(G_VARIANT_TYPE("a(sssia(s))"));

    for (int i = 0; i < count; i++)
    {
        GVariant *tuple = pack_option(&options[i]);
        g_variant_builder_add_value(builder, tuple);
        //g_variant_unref(tuple);
    }
    variant = g_variant_builder_end(builder);
    
    print_backend_complete_get_all_options(interface, invocation, count, variant, media_count, media_variant);
    free_options(count, options);
    return TRUE;
}

static gboolean on_handle_get_active_jobs_count(PrintBackend *interface,
                                                GDBusMethodInvocation *invocation,
                                                const gchar *printer_name,
                                                gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    print_backend_complete_get_active_jobs_count(interface, invocation, get_active_jobs_count(p));
    return TRUE;
}
static gboolean on_handle_get_all_jobs(PrintBackend *interface,
                                       GDBusMethodInvocation *invocation,
                                       gboolean active_only,
                                       gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation); /// potential risk
    int n;
    GVariant *variant = get_all_jobs(b, dialog_name, &n, active_only);
    print_backend_complete_get_all_jobs(interface, invocation, n, variant);
    return TRUE;
}
static gboolean on_handle_cancel_job(PrintBackend *interface,
                                     GDBusMethodInvocation *invocation,
                                     const gchar *job_id,
                                     const gchar *printer_name,
                                     gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    int jobid = atoi(job_id); /**to do. check if given job id is integer */
    PrinterCUPS *p = get_printer_by_name(b, dialog_name, printer_name);
    gboolean status = cancel_job(p, jobid);
    print_backend_complete_cancel_job(interface, invocation, status);
    return TRUE;
}

static gboolean on_handle_get_default_printer(PrintBackend *interface,
                                              GDBusMethodInvocation *invocation,
                                              gpointer user_data)
{
    char *def = get_default_printer(b);
    printf("%s\n", def);
    print_backend_complete_get_default_printer(interface, invocation, def);
    return TRUE;
}

static gboolean on_handle_keep_alive(PrintBackend *interface,
                                     GDBusMethodInvocation *invocation,
                                     gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    Dialog *d = find_dialog(b, dialog_name);
    d->keep_alive = TRUE;
    print_backend_complete_keep_alive(interface, invocation);
    return TRUE;
}
static gboolean on_handle_replace(PrintBackend *interface,
                                  GDBusMethodInvocation *invocation,
                                  const gchar *previous_name,
                                  gpointer user_data)
{
    const char *dialog_name = g_dbus_method_invocation_get_sender(invocation);
    Dialog *d = find_dialog(b, previous_name);
    if (d != NULL)
    {
        g_hash_table_steal(b->dialogs, previous_name);
        g_hash_table_insert(b->dialogs, cpdbGetStringCopy(dialog_name), d);
        g_message("Replaced %s --> %s\n", previous_name, dialog_name);
    }
    print_backend_complete_replace(interface, invocation);
    return TRUE;
}
void connect_to_signals()
{
    PrintBackend *skeleton = b->skeleton;
    g_signal_connect(skeleton,                               //instance
                     "handle-get-printer-list",              //signal name
                     G_CALLBACK(on_handle_get_printer_list), //callback
                     NULL);
    g_signal_connect(skeleton,                              //instance
                     "handle-get-all-options",              //signal name
                     G_CALLBACK(on_handle_get_all_options), //callback
                     NULL);
    g_signal_connect(skeleton,                   //instance
                     "handle-ping",              //signal name
                     G_CALLBACK(on_handle_ping), //callback
                     NULL);
    g_signal_connect(skeleton,                                  //instance
                     "handle-get-default-printer",              //signal name
                     G_CALLBACK(on_handle_get_default_printer), //callback
                     NULL);
    g_signal_connect(skeleton,                         //instance
                     "handle-print-file",              //signal name
                     G_CALLBACK(on_handle_print_file), //callback
                     NULL);
    g_signal_connect(skeleton,                                //instance
                     "handle-get-printer-state",              //signal name
                     G_CALLBACK(on_handle_get_printer_state), //callback
                     NULL);
    g_signal_connect(skeleton,                                //instance
                     "handle-is-accepting-jobs",              //signal name
                     G_CALLBACK(on_handle_is_accepting_jobs), //callback
                     NULL);
    g_signal_connect(skeleton,                                    //instance
                     "handle-get-active-jobs-count",              //signal name
                     G_CALLBACK(on_handle_get_active_jobs_count), //callback
                     NULL);
    g_signal_connect(skeleton,                           //instance
                     "handle-get-all-jobs",              //signal name
                     G_CALLBACK(on_handle_get_all_jobs), //callback
                     NULL);
    g_signal_connect(skeleton,                         //instance
                     "handle-cancel-job",              //signal name
                     G_CALLBACK(on_handle_cancel_job), //callback
                     NULL);
    g_signal_connect(skeleton,                         //instance
                     "handle-keep-alive",              //signal name
                     G_CALLBACK(on_handle_keep_alive), //callback
                     NULL);
    g_signal_connect(skeleton,                      //instance
                     "handle-replace",              //signal name
                     G_CALLBACK(on_handle_replace), //callback
                     NULL);
    g_signal_connect(skeleton,                                      //instance
                     "handle-get-option-translation",               //signal name
                     G_CALLBACK(on_handle_get_option_translation),  //callback
                     NULL);
    g_signal_connect(skeleton,                                      //instance
                     "handle-get-choice-translation",               //signal name
                     G_CALLBACK(on_handle_get_choice_translation),  //callback
                     NULL);
    g_signal_connect(skeleton,                                      //instance
                     "handle-get-group-translation",                //signal name
                     G_CALLBACK(on_handle_get_group_translation),   //callback
                     NULL);
    g_signal_connect(skeleton,
                     "handle-get-all-translations",
                     G_CALLBACK(on_handle_get_all_translations),
                     NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       CPDB_SIGNAL_STOP_BACKEND,              //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_stop_backend,                  //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       CPDB_SIGNAL_HIDE_REMOTE,         		 //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_hide_remote_printers,          //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       CPDB_SIGNAL_UNHIDE_REMOTE,        	 //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_unhide_remote_printers,        //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       CPDB_SIGNAL_HIDE_TEMP,     	         //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_hide_temp_printers,            //callback
                                       NULL,                             //user_data
                                       NULL);
    g_dbus_connection_signal_subscribe(b->dbus_connection,
                                       NULL,                             //Sender name
                                       "org.openprinting.PrintFrontend", //Sender interface
                                       CPDB_SIGNAL_UNHIDE_TEMP,          	 //Signal name
                                       NULL,                             /**match on all object paths**/
                                       NULL,                             /**match on all arguments**/
                                       0,                                //Flags
                                       on_unhide_temp_printers,          //callback
                                       NULL,                             //user_data
                                       NULL);
}
