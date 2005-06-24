/*
 *  Off-the-Record Messaging plugin for gaim
 *  Copyright (C) 2004-2005  Nikita Borisov and Ian Goldberg
 *                           <otr@cypherpunks.ca>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* system headers */
#include <gtk/gtk.h>

/* libgcrypt headers */
#include <gcrypt.h>

/* libotr headers */
#include <libotr/privkey.h>

/* gaim headers */
#include "util.h"
#include "account.h"
#include "notify.h"
#include "gtkutils.h"

/* gaim-otr headers */
#include "dialogs.h"
#include "ui.h"
#include "otr-plugin.h"

struct otroptionsdata {
    GtkWidget *enablebox;
    GtkWidget *automaticbox;
    GtkWidget *onlyprivatebox;
};

static struct {
    GtkWidget *accountmenu;
    GtkWidget *fprint_label;
    GtkWidget *generate_button;
    GtkWidget *scrollwin;
    GtkWidget *keylist;
    gint sortcol, sortdir;
    Fingerprint *selected_fprint;
    GtkWidget *connect_button;
    GtkWidget *disconnect_button;
    GtkWidget *forget_button;
    GtkWidget *verify_button;
    struct otroptionsdata oo;
} ui_layout;

static void account_menu_changed_cb(GtkWidget *item, GaimAccount *account,
	void *data)
{
    const char *accountname;
    const char *protocol;
    GtkWidget *fprint = ui_layout.fprint_label;
    char s[100];
    char *fingerprint;
    
    if (account) {
	char fingerprint_buf[45];
	accountname = gaim_account_get_username(account);
	protocol = gaim_account_get_protocol_id(account);
	fingerprint = otrl_privkey_fingerprint(otrg_plugin_userstate,
		fingerprint_buf, accountname, protocol);

	if (fingerprint) {
	    sprintf(s, "Fingerprint: %.80s", fingerprint);
	    if (ui_layout.generate_button)
		gtk_widget_set_sensitive(ui_layout.generate_button, 0);
	} else {
	    sprintf(s, "No key present");
	    if (ui_layout.generate_button)
		gtk_widget_set_sensitive(ui_layout.generate_button, 1);
	}
    } else {
	sprintf(s, "No account available");
	if (ui_layout.generate_button)
	    gtk_widget_set_sensitive(ui_layout.generate_button, 0);
    }
    if (fprint) {
	gtk_label_set_text(GTK_LABEL(fprint), s);
	gtk_widget_show(fprint);
    }
}

static GtkWidget *accountmenu_get_selected_item(void)
{
    GtkWidget *menu;

    if (ui_layout.accountmenu == NULL) return NULL;

    menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(ui_layout.accountmenu));
    return gtk_menu_get_active(GTK_MENU(menu));
}

static GaimAccount *item_get_account(GtkWidget *item)
{
    if (!item) return NULL;
    return g_object_get_data(G_OBJECT(item), "account");
}

/* Call this function when the DSA key is updated; it will redraw the
 * UI, if visible. */
static void otrg_gtk_ui_update_fingerprint(void)
{
    GtkWidget *item;
    GaimAccount *account;
    gpointer user_data;

    item = accountmenu_get_selected_item();

    if (!item) return;

    account   = item_get_account(item);
    user_data = g_object_get_data(G_OBJECT(ui_layout.accountmenu),
	    "user_data");

    account_menu_changed_cb(item, account, user_data);
}

static void account_menu_added_removed_cb(GaimAccount *account, void *data)
{
    otrg_gtk_ui_update_fingerprint();
}

static void clist_all_unselected(void)
{
    gtk_widget_set_sensitive(ui_layout.connect_button, 0);
    gtk_widget_set_sensitive(ui_layout.disconnect_button, 0);
    gtk_widget_set_sensitive(ui_layout.forget_button, 0);
    gtk_widget_set_sensitive(ui_layout.verify_button, 0);
    ui_layout.selected_fprint = NULL;
}

/* Update the keylist, if it's visible */
static void otrg_gtk_ui_update_keylist(void)
{
    gchar *titles[5];
    char hash[45];
    ConnContext * context;
    Fingerprint * fingerprint;
    int selected_row = -1;

    GtkWidget *keylist = ui_layout.keylist;

    if (keylist == NULL)
	return;

    gtk_clist_freeze(GTK_CLIST(keylist));
    gtk_clist_clear(GTK_CLIST(keylist));

    for (context = otrg_plugin_userstate->context_root; context != NULL;
	    context = context->next) {
	int i;
	GaimPlugin *p;
	char *proto_name;
	fingerprint = context->fingerprint_root.next;
	/* If there's no fingerprint, don't add it to the known
	 * fingerprints list */
	while(fingerprint) {
	    titles[0] = context->username;
	    if (context->state == CONN_CONNECTED &&
		    context->active_fingerprint != fingerprint) {
		titles[1] = "Unused";
	    } else {
		titles[1] =
		    (gchar *) otrl_context_statestr[context->state];
	    }
	    titles[2] = (fingerprint->trust && fingerprint->trust[0]) ?
		"Yes" : "No";
	    otrl_privkey_hash_to_human(hash, fingerprint->fingerprint);
	    titles[3] = hash;
	    p = gaim_find_prpl(context->protocol);
	    proto_name = (p && p->info->name) ? p->info->name : "Unknown";
	    titles[4] = g_strdup_printf("%s (%s)", context->accountname,
		proto_name);
	    i = gtk_clist_append(GTK_CLIST(keylist), titles);
	    g_free(titles[4]);
	    gtk_clist_set_row_data(GTK_CLIST(keylist), i, fingerprint);
	    if (ui_layout.selected_fprint == fingerprint) {
		selected_row = i;
	    }
	    fingerprint = fingerprint->next;
	}
    }

    if (selected_row >= 0) {
	gtk_clist_select_row(GTK_CLIST(keylist), selected_row, 0);
    } else {
	clist_all_unselected();
    }

    gtk_clist_sort(GTK_CLIST(keylist));

    gtk_clist_thaw(GTK_CLIST(keylist));

}

static void generate(GtkWidget *widget, gpointer data)
{
    GaimAccount *account;
    account = item_get_account(accountmenu_get_selected_item());
	
    if (account == NULL) return;
	
    otrg_plugin_create_privkey(gaim_account_get_username(account),
	    gaim_account_get_protocol_id(account));
}

static void ui_destroyed(GtkObject *object)
{
    /* If this is called, we need to invalidate the stored pointers in
     * the ui_layout struct. */
    ui_layout.accountmenu = NULL;
    ui_layout.fprint_label = NULL;
    ui_layout.generate_button = NULL;
    ui_layout.scrollwin = NULL;
    ui_layout.keylist = NULL;
    ui_layout.selected_fprint = NULL;
    ui_layout.connect_button = NULL;
    ui_layout.disconnect_button = NULL;
    ui_layout.forget_button = NULL;
    ui_layout.verify_button = NULL;
    ui_layout.oo.enablebox = NULL;
    ui_layout.oo.automaticbox = NULL;
    ui_layout.oo.onlyprivatebox = NULL;
}

static void clist_selected(GtkWidget *widget, gint row, gint column,
	GdkEventButton *event, gpointer data)
{
    int connect_sensitive = 0;
    int disconnect_sensitive = 0;
    int forget_sensitive = 0;
    int verify_sensitive = 0;
    Fingerprint *f = gtk_clist_get_row_data(GTK_CLIST(ui_layout.keylist),
	    row);
    if (f && f->context->state == CONN_CONNECTED &&
	    f->context->active_fingerprint == f) {
	disconnect_sensitive = 1;
    }
    if (f && f->context->state == CONN_SETUP) {
	disconnect_sensitive = 1;
    }
    if (f && f->context->state == CONN_CONNECTED &&
	    f->context->active_fingerprint != f) {
	forget_sensitive = 1;
    }
    if (f && f->context->state == CONN_UNCONNECTED) {
	forget_sensitive = 1;
    }
    if (f && f->context->state == CONN_UNCONNECTED) {
	connect_sensitive = 1;
    }
    if (f) {
	verify_sensitive = 1;
    }
    gtk_widget_set_sensitive(ui_layout.connect_button,
	    connect_sensitive);
    gtk_widget_set_sensitive(ui_layout.disconnect_button,
	    disconnect_sensitive);
    gtk_widget_set_sensitive(ui_layout.forget_button, forget_sensitive);
    gtk_widget_set_sensitive(ui_layout.verify_button, verify_sensitive);
    ui_layout.selected_fprint = f;
}

static void clist_unselected(GtkWidget *widget, gint row, gint column,
	GdkEventButton *event, gpointer data)
{
    clist_all_unselected();
}

static int fngsortval(Fingerprint *f)
{
    if (f->context->state == CONN_CONNECTED &&
	    f->context->active_fingerprint == f) {
	return 0;
    } else if (f->context->state == CONN_SETUP) {
	return 1;
    } else if (f->context->state == CONN_UNCONNECTED) {
	return 2;
    } else {
	return 3;
    }
}

static gint statuscmp(GtkCList *clist, gconstpointer ptr1, gconstpointer ptr2)
{
    const GtkCListRow *a = ptr1;
    const GtkCListRow *b = ptr2;
    int as = fngsortval(a->data);
    int bs = fngsortval(b->data);
    return (as - bs);
}

static void clist_click_column(GtkCList *clist, gint column, gpointer data)
{
    if (ui_layout.sortcol == column) {
	ui_layout.sortdir = -(ui_layout.sortdir);
    } else {
	ui_layout.sortcol = column;
	ui_layout.sortdir = 1;
    }

    gtk_clist_set_sort_column(clist, ui_layout.sortcol);
    gtk_clist_set_sort_type(clist,
	    ui_layout.sortdir == 1 ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);
    if (column == 1) {
	gtk_clist_set_compare_func(clist, statuscmp);
    } else {
	/* Just use the default compare function for the rest of the
	 * columns */
	gtk_clist_set_compare_func(clist, NULL);
    }
    gtk_clist_sort(clist);
}

static void connect_connection(GtkWidget *widget, gpointer data)
{
    /* Send an OTR Query to the other side. */
    ConnContext *context;

    if (ui_layout.selected_fprint == NULL) return;

    context = ui_layout.selected_fprint->context;
    otrg_ui_connect_connection(context);
}

static void disconnect_connection(GtkWidget *widget, gpointer data)
{
    /* Forget whatever state we've got with this context */
    ConnContext *context;

    if (ui_layout.selected_fprint == NULL) return;

    context = ui_layout.selected_fprint->context;
    if (context == NULL) return;
	
    /* Don't do anything with fingerprints other than the active one
     * if we're in the CONNECTED state */
    if (context->state == CONN_CONNECTED &&
	    context->active_fingerprint != ui_layout.selected_fprint) {
	return;
    }
	
    otrg_ui_disconnect_connection(context);
}

static void forget_fingerprint(GtkWidget *widget, gpointer data)
{
    Fingerprint *fingerprint = ui_layout.selected_fprint;

    otrg_ui_forget_fingerprint(fingerprint);
}

static void verify_fingerprint(GtkWidget *widget, gpointer data)
{
    Fingerprint *fingerprint = ui_layout.selected_fprint;

    otrg_dialog_verify_fingerprint(fingerprint);
}

static void otroptions_clicked_cb(GtkButton *button, struct otroptionsdata *oo)
{
    gtk_widget_set_sensitive(oo->enablebox, TRUE);
    if (gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(oo->enablebox))) {
	gtk_widget_set_sensitive(oo->automaticbox, TRUE);
	if (gtk_toggle_button_get_active(
		    GTK_TOGGLE_BUTTON(oo->automaticbox))) {
	    gtk_widget_set_sensitive(oo->onlyprivatebox, TRUE);
	} else {
	    gtk_widget_set_sensitive(oo->onlyprivatebox, FALSE);
	}
    } else {
	gtk_widget_set_sensitive(oo->automaticbox, FALSE);
	gtk_widget_set_sensitive(oo->onlyprivatebox, FALSE);
    }
}

static void create_otroption_buttons(struct otroptionsdata *oo,
	GtkWidget *vbox)
{
    GtkWidget *tempbox1, *tempbox2;

    oo->enablebox = gtk_check_button_new_with_label("Enable private "
	    "messaging");
    oo->automaticbox = gtk_check_button_new_with_label("Automatically "
	    "initiate private messaging");
    oo->onlyprivatebox = gtk_check_button_new_with_label("Require private "
	    "messaging");

    gtk_box_pack_start(GTK_BOX(vbox), oo->enablebox,
	    FALSE, FALSE, 0);
    tempbox1 = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), tempbox1,
	    FALSE, FALSE, 0);
    tempbox2 = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tempbox1), tempbox2, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(tempbox2), oo->automaticbox,
	    FALSE, FALSE, 0);
    tempbox1 = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tempbox2), tempbox1, FALSE, FALSE, 0);
    tempbox2 = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tempbox1), tempbox2, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(tempbox2), oo->onlyprivatebox,
	    FALSE, FALSE, 0);

    g_signal_connect(G_OBJECT(oo->enablebox), "clicked",
		     G_CALLBACK(otroptions_clicked_cb), oo);
    g_signal_connect(G_OBJECT(oo->automaticbox), "clicked",
		     G_CALLBACK(otroptions_clicked_cb), oo);
    g_signal_connect(G_OBJECT(oo->onlyprivatebox), "clicked",
		     G_CALLBACK(otroptions_clicked_cb), oo);
}

/* Load the global OTR prefs */
static void otrg_gtk_ui_global_prefs_load(gboolean *enabledp,
	gboolean *automaticp, gboolean *onlyprivatep)
{
    if (gaim_prefs_exists("/OTR/enabled")) {
	*enabledp = gaim_prefs_get_bool("/OTR/enabled");
	*automaticp = gaim_prefs_get_bool("/OTR/automatic");
	*onlyprivatep = gaim_prefs_get_bool("/OTR/onlyprivate");
    } else {
	*enabledp = TRUE;
	*automaticp = TRUE;
	*onlyprivatep = FALSE;
    }
}

/* Save the global OTR prefs */
static void otrg_gtk_ui_global_prefs_save(gboolean enabled,
	gboolean automatic, gboolean onlyprivate)
{
    if (! gaim_prefs_exists("/OTR")) {
	gaim_prefs_add_none("/OTR");
    }
    gaim_prefs_set_bool("/OTR/enabled", enabled);
    gaim_prefs_set_bool("/OTR/automatic", automatic);
    gaim_prefs_set_bool("/OTR/onlyprivate", onlyprivate);
}

/* Load the OTR prefs for a particular buddy */
static void otrg_gtk_ui_buddy_prefs_load(GaimBuddy *buddy,
	gboolean *usedefaultp, gboolean *enabledp, gboolean *automaticp,
	gboolean *onlyprivatep)
{
    GaimBlistNode *node = &(buddy->node);

    *usedefaultp = ! gaim_blist_node_get_bool(node, "OTR/overridedefault");

    if (*usedefaultp) {
	otrg_gtk_ui_global_prefs_load(enabledp, automaticp, onlyprivatep);
    } else {
	*enabledp = gaim_blist_node_get_bool(node, "OTR/enabled");
	*automaticp = gaim_blist_node_get_bool(node, "OTR/automatic");
	*onlyprivatep = gaim_blist_node_get_bool(node, "OTR/onlyprivate");
    }
}

/* Save the OTR prefs for a particular buddy */
static void otrg_gtk_ui_buddy_prefs_save(GaimBuddy *buddy,
	gboolean usedefault, gboolean enabled, gboolean automatic,
	gboolean onlyprivate)
{
    GaimBlistNode *node = &(buddy->node);

    gaim_blist_node_set_bool(node, "OTR/overridedefault", !usedefault);
    gaim_blist_node_set_bool(node, "OTR/enabled", enabled);
    gaim_blist_node_set_bool(node, "OTR/automatic", automatic);
    gaim_blist_node_set_bool(node, "OTR/onlyprivate", onlyprivate);
}

static void load_otroptions(struct otroptionsdata *oo)
{
    gboolean otrenabled;
    gboolean otrautomatic;
    gboolean otronlyprivate;

    otrg_gtk_ui_global_prefs_load(&otrenabled, &otrautomatic, &otronlyprivate);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(oo->enablebox),
	    otrenabled);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(oo->automaticbox),
	    otrautomatic);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(oo->onlyprivatebox),
	    otronlyprivate);

    otroptions_clicked_cb(GTK_BUTTON(oo->enablebox), oo);
}

/* Create the privkeys UI, and pack it into the vbox */
static void make_privkeys_ui(GtkWidget *vbox)
{
    GtkWidget *fbox;
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *frame;

    frame = gtk_frame_new("My private keys");
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

    fbox = gtk_vbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(fbox), 10);
    gtk_container_add(GTK_CONTAINER(frame), fbox);

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(fbox), hbox, FALSE, FALSE, 0);
    label = gtk_label_new("Key for account:");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    ui_layout.accountmenu = gaim_gtk_account_option_menu_new(NULL, 1,
	    G_CALLBACK(account_menu_changed_cb), NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), ui_layout.accountmenu, TRUE, TRUE, 0);

    /* Make sure we notice if the menu changes because an account has
     * been added or removed */
    gaim_signal_connect(gaim_accounts_get_handle(), "account-added",
	    ui_layout.accountmenu,
	    GAIM_CALLBACK(account_menu_added_removed_cb), NULL);
    gaim_signal_connect(gaim_accounts_get_handle(), "account-removed",
	    ui_layout.accountmenu,
	    GAIM_CALLBACK(account_menu_added_removed_cb), NULL);

    ui_layout.fprint_label = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(ui_layout.fprint_label), 1);
    gtk_box_pack_start(GTK_BOX(fbox), ui_layout.fprint_label,
	    FALSE, FALSE, 0);

    ui_layout.generate_button = gtk_button_new();
    gtk_signal_connect(GTK_OBJECT(ui_layout.generate_button), "clicked",
	    GTK_SIGNAL_FUNC(generate), NULL);

    label = gtk_label_new("Generate");
    gtk_container_add(GTK_CONTAINER(ui_layout.generate_button), label);

    otrg_gtk_ui_update_fingerprint();

    gtk_box_pack_start(GTK_BOX(fbox), ui_layout.generate_button,
	    FALSE, FALSE, 0);
}

/* Save the global OTR options whenever they're clicked */
static void otroptions_save_cb(GtkButton *button, struct otroptionsdata *oo)
{
    otrg_gtk_ui_global_prefs_save(
	    gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(oo->enablebox)),
	    gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(oo->automaticbox)),
	    gtk_toggle_button_get_active(
		GTK_TOGGLE_BUTTON(oo->onlyprivatebox)));

    otrg_dialog_resensitize_all();
}

/* Make the options UI, and pack it into the vbox */
static void make_options_ui(GtkWidget *vbox)
{
    GtkWidget *fbox;
    GtkWidget *frame;

    frame = gtk_frame_new("Default OTR Settings");
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

    fbox = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(fbox), 10);
    gtk_container_add(GTK_CONTAINER(frame), fbox);

    create_otroption_buttons(&(ui_layout.oo), fbox);

    load_otroptions(&(ui_layout.oo));

    g_signal_connect(G_OBJECT(ui_layout.oo.enablebox), "clicked",
		     G_CALLBACK(otroptions_save_cb), &(ui_layout.oo));
    g_signal_connect(G_OBJECT(ui_layout.oo.automaticbox), "clicked",
		     G_CALLBACK(otroptions_save_cb), &(ui_layout.oo));
    g_signal_connect(G_OBJECT(ui_layout.oo.onlyprivatebox), "clicked",
		     G_CALLBACK(otroptions_save_cb), &(ui_layout.oo));
}

/* Create the fingerprint UI, and pack it into the vbox */
static void make_fingerprints_ui(GtkWidget *vbox)
{
    GtkWidget *hbox;
    GtkWidget *table;
    GtkWidget *label;
    char *titles[5] = { "Screenname", "Status", "Verified",
	"Fingerprint", "Account" };

    ui_layout.scrollwin = gtk_scrolled_window_new(0, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ui_layout.scrollwin), 
            GTK_POLICY_ALWAYS, GTK_POLICY_ALWAYS);

    ui_layout.keylist = gtk_clist_new_with_titles(5, titles);
    gtk_clist_set_column_width(GTK_CLIST(ui_layout.keylist), 0, 90);
    gtk_clist_set_column_width(GTK_CLIST(ui_layout.keylist), 1, 90);
    gtk_clist_set_column_width(GTK_CLIST(ui_layout.keylist), 2, 60);
    gtk_clist_set_column_width(GTK_CLIST(ui_layout.keylist), 3, 400);
    gtk_clist_set_column_width(GTK_CLIST(ui_layout.keylist), 4, 200);
    gtk_clist_set_selection_mode(GTK_CLIST(ui_layout.keylist),
	    GTK_SELECTION_SINGLE);
    gtk_clist_column_titles_active(GTK_CLIST(ui_layout.keylist));

    gtk_container_add(GTK_CONTAINER(ui_layout.scrollwin), ui_layout.keylist);
    gtk_box_pack_start(GTK_BOX(vbox), ui_layout.scrollwin,
	    TRUE, TRUE, 0);

    otrg_gtk_ui_update_keylist();

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

    table = gtk_table_new(2, 2, TRUE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 5);
    gtk_table_set_col_spacings(GTK_TABLE(table), 20);

    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(""), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(""), TRUE, TRUE, 0);

    ui_layout.connect_button = gtk_button_new();
    gtk_signal_connect(GTK_OBJECT(ui_layout.connect_button), "clicked",
	    GTK_SIGNAL_FUNC(connect_connection), NULL);
    label = gtk_label_new("Start private connection");
    gtk_container_add(GTK_CONTAINER(ui_layout.connect_button), label);
    gtk_table_attach_defaults(GTK_TABLE(table), ui_layout.connect_button,
	    0, 1, 0, 1);

    ui_layout.disconnect_button = gtk_button_new();
    gtk_signal_connect(GTK_OBJECT(ui_layout.disconnect_button), "clicked",
	    GTK_SIGNAL_FUNC(disconnect_connection), NULL);
    label = gtk_label_new("End private connection");
    gtk_container_add(GTK_CONTAINER(ui_layout.disconnect_button), label);
    gtk_table_attach_defaults(GTK_TABLE(table), ui_layout.disconnect_button,
	    0, 1, 1, 2);

    ui_layout.verify_button = gtk_button_new();
    gtk_signal_connect(GTK_OBJECT(ui_layout.verify_button), "clicked",
	    GTK_SIGNAL_FUNC(verify_fingerprint), NULL);
    label = gtk_label_new("Verify fingerprint");
    gtk_container_add(GTK_CONTAINER(ui_layout.verify_button), label);
    gtk_table_attach_defaults(GTK_TABLE(table), ui_layout.verify_button,
	    1, 2, 0, 1);

    ui_layout.forget_button = gtk_button_new();
    gtk_signal_connect(GTK_OBJECT(ui_layout.forget_button), "clicked",
	    GTK_SIGNAL_FUNC(forget_fingerprint), NULL);
    label = gtk_label_new("Forget fingerprint");
    gtk_container_add(GTK_CONTAINER(ui_layout.forget_button), label);
    gtk_table_attach_defaults(GTK_TABLE(table), ui_layout.forget_button,
	    1, 2, 1, 2);

    gtk_signal_connect(GTK_OBJECT(vbox), "destroy",
	    GTK_SIGNAL_FUNC(ui_destroyed), NULL);

    /* Handle selections and deselections */
    gtk_signal_connect(GTK_OBJECT(ui_layout.keylist), "select_row",
	    GTK_SIGNAL_FUNC(clist_selected), NULL);
    gtk_signal_connect(GTK_OBJECT(ui_layout.keylist), "unselect_row",
	    GTK_SIGNAL_FUNC(clist_unselected), NULL);

    /* Handle column sorting */
    gtk_signal_connect(GTK_OBJECT(ui_layout.keylist), "click-column",
	    GTK_SIGNAL_FUNC(clist_click_column), NULL);
    ui_layout.sortcol = 0;
    ui_layout.sortdir = 1;

    clist_all_unselected();
}

/* Construct the OTR UI widget */
GtkWidget* otrg_gtk_ui_make_widget(GaimPlugin *plugin)
{
    GtkWidget *vbox = gtk_vbox_new(FALSE, 5);
    GtkWidget *fingerprintbox = gtk_vbox_new(FALSE, 5);
    GtkWidget *configbox = gtk_vbox_new(FALSE, 5);
    GtkWidget *notebook = gtk_notebook_new();

    gtk_container_set_border_width(GTK_CONTAINER(vbox), 2);
    gtk_container_set_border_width(GTK_CONTAINER(fingerprintbox), 5);
    gtk_container_set_border_width(GTK_CONTAINER(configbox), 5);

    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    make_privkeys_ui(configbox);

    make_options_ui(configbox);

    /*
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(confwindow), vbox);
    */

    make_fingerprints_ui(fingerprintbox);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fingerprintbox,
	    gtk_label_new("Known fingerprints"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), configbox,
	    gtk_label_new("Config"));

    gtk_widget_show_all(vbox);

    return vbox;
}

struct cbdata {
    GtkWidget *dialog;
    GaimBuddy *buddy;
    GtkWidget *defaultbox;
    struct otroptionsdata oo;
};

static void default_clicked_cb(GtkButton *button, struct cbdata *data)
{
    gboolean defaultset =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->defaultbox));
    if (defaultset) {
	gtk_widget_set_sensitive(data->oo.enablebox, FALSE);
	gtk_widget_set_sensitive(data->oo.automaticbox, FALSE);
	gtk_widget_set_sensitive(data->oo.onlyprivatebox, FALSE);
    } else {
	otroptions_clicked_cb(button, &(data->oo));
    }
}

static void load_buddyprefs(struct cbdata *data)
{
    gboolean usedefault, enabled, automatic, onlyprivate;

    otrg_gtk_ui_buddy_prefs_load(data->buddy, &usedefault, &enabled,
	    &automatic, &onlyprivate);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->defaultbox),
	    usedefault);

    if (usedefault) {
	/* Load the global defaults */
	load_otroptions(&(data->oo));
    } else {
	/* We've got buddy-specific prefs */
	gtk_toggle_button_set_active(
		GTK_TOGGLE_BUTTON(data->oo.enablebox), enabled);
	gtk_toggle_button_set_active(
		GTK_TOGGLE_BUTTON(data->oo.automaticbox), automatic);
	gtk_toggle_button_set_active(
		GTK_TOGGLE_BUTTON(data->oo.onlyprivatebox), onlyprivate);
    }

    default_clicked_cb(GTK_BUTTON(data->defaultbox), data);
}

static void config_buddy_destroy_cb(GtkWidget *w, struct cbdata *data)
{
    free(data);
}

static void config_buddy_clicked_cb(GtkButton *button, struct cbdata *data)
{
    gboolean enabled = gtk_toggle_button_get_active(
			     GTK_TOGGLE_BUTTON(data->oo.enablebox));
    
    /* Apply the changes */
    otrg_gtk_ui_buddy_prefs_save(data->buddy,
	 gtk_toggle_button_get_active(
	     GTK_TOGGLE_BUTTON(data->defaultbox)),
	 enabled,
	 gtk_toggle_button_get_active(
	     GTK_TOGGLE_BUTTON(data->oo.automaticbox)),
	 gtk_toggle_button_get_active(
	     GTK_TOGGLE_BUTTON(data->oo.onlyprivatebox)));

    otrg_dialog_resensitize_all();
}

static void config_buddy_response_cb(GtkDialog *dialog, gint resp,
	struct cbdata *data)
{
    gtk_widget_destroy(data->dialog);
}

static void otrg_gtk_ui_config_buddy(GaimBuddy *buddy)
{
    GtkWidget *dialog;
    GtkWidget *label;
    char *label_text;
    struct cbdata *data = malloc(sizeof(struct cbdata));

    if (!data) return;

    dialog = gtk_dialog_new_with_buttons("OTR Settings",
					 NULL, 0,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 NULL);
    gtk_window_set_accept_focus(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_role(GTK_WINDOW(dialog), "otr_options");

    gtk_container_set_border_width(GTK_CONTAINER(dialog), 6);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_dialog_set_has_separator(GTK_DIALOG(dialog), FALSE);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 0);
    gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), 0);

    data->dialog = dialog;
    data->buddy = buddy;

    /* Set the title */

    label_text = g_strdup_printf("<span weight=\"bold\" size=\"larger\">"
	    "OTR Settings for %s</span>", gaim_buddy_get_contact_alias(buddy));

    label = gtk_label_new(NULL);

    gtk_label_set_markup(GTK_LABEL(label), label_text);
    g_free(label_text);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label,
	    FALSE, FALSE, 5);

    /* Make the cascaded checkboxes */

    data->defaultbox = gtk_check_button_new_with_label("Use default "
	    "OTR settings for this buddy");

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), data->defaultbox,
	    FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), gtk_hseparator_new(),
	    FALSE, FALSE, 5);

    create_otroption_buttons(&(data->oo), GTK_DIALOG(dialog)->vbox);

    g_signal_connect(G_OBJECT(data->defaultbox), "clicked",
		     G_CALLBACK(default_clicked_cb), data);
    g_signal_connect(G_OBJECT(data->defaultbox), "clicked",
		     G_CALLBACK(config_buddy_clicked_cb), data);
    g_signal_connect(G_OBJECT(data->oo.enablebox), "clicked",
		     G_CALLBACK(config_buddy_clicked_cb), data);
    g_signal_connect(G_OBJECT(data->oo.automaticbox), "clicked",
		     G_CALLBACK(config_buddy_clicked_cb), data);
    g_signal_connect(G_OBJECT(data->oo.onlyprivatebox), "clicked",
		     G_CALLBACK(config_buddy_clicked_cb), data);

    /* Set the inital states of the buttons */
    load_buddyprefs(data);

    g_signal_connect(G_OBJECT(dialog), "destroy",
		     G_CALLBACK(config_buddy_destroy_cb), data);
    g_signal_connect(G_OBJECT(dialog), "response",
		     G_CALLBACK(config_buddy_response_cb), data);

    gtk_widget_show_all(dialog);
}

/* Calculate the policy for a particular account / username */
static OtrlPolicy otrg_gtk_ui_find_policy(GaimAccount *account,
	const char *name)
{
    GaimBuddy *buddy;
    gboolean otrenabled, otrautomatic, otronlyprivate;
    gboolean buddyusedefault, buddyenabled, buddyautomatic, buddyonlyprivate;
    OtrlPolicy policy = OTRL_POLICY_DEFAULT;
    
    /* Get the default policy */
    otrg_gtk_ui_global_prefs_load(&otrenabled, &otrautomatic, &otronlyprivate);

    if (otrenabled) {
	if (otrautomatic) {
	    if (otronlyprivate) {
		policy = OTRL_POLICY_ALWAYS;
	    } else {
		policy = OTRL_POLICY_OPPORTUNISTIC;
	    }
	} else {
	    policy = OTRL_POLICY_MANUAL;
	}
    } else {
	policy = OTRL_POLICY_NEVER;
    }

    buddy = gaim_find_buddy(account, name);
    if (!buddy) return policy;

    /* Get the buddy-specific policy, if present */
    otrg_gtk_ui_buddy_prefs_load(buddy, &buddyusedefault, &buddyenabled,
	    &buddyautomatic, &buddyonlyprivate);

    if (buddyusedefault) return policy;

    if (buddyenabled) {
	if (buddyautomatic) {
	    if (buddyonlyprivate) {
		policy = OTRL_POLICY_ALWAYS;
	    } else {
		policy = OTRL_POLICY_OPPORTUNISTIC;
	    }
	} else {
	    policy = OTRL_POLICY_MANUAL;
	}
    } else {
	policy = OTRL_POLICY_NEVER;
    }

    return policy;
}

static const OtrgUiUiOps gtk_ui_ui_ops = {
    otrg_gtk_ui_update_fingerprint,
    otrg_gtk_ui_update_keylist,
    otrg_gtk_ui_config_buddy,
    otrg_gtk_ui_find_policy
};

/* Get the GTK UI ops */
const OtrgUiUiOps *otrg_gtk_ui_get_ui_ops(void)
{
    return &gtk_ui_ui_ops;
}
