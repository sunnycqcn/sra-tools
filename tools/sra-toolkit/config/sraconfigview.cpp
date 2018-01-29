/*===========================================================================
*
*                            PUBLIC DOMAIN NOTICE
*               National Center for Biotechnology Information
*
*  This software/database is a "United States Government Work" under the
*  terms of the United States Copyright Act.  It was written as part of
*  the author's official duties as a United States Government employee and
*  thus cannot be copyrighted.  This software/database is freely available
*  to the public for use. The National Library of Medicine and the U.S.
*  Government have not placed any restriction on its use or reproduction.
*
*  Although all reasonable efforts have been taken to ensure the accuracy
*  and reliability of the software and data, the NLM and the U.S.
*  Government do not and cannot warrant the performance or results that
*  may be obtained by using this software or data. The NLM and the U.S.
*  Government disclaim all warranties, express or implied, including
*  warranties of performance, merchantability or fitness for any particular
*  purpose.
*
*  Please cite the author in any work or product based on this material.
*
* ===========================================================================
*
*/

#include "sraconfigview.h"
#include "sraconfigmodel.h"
#include "../vdb-config/vdb-config-model.hpp"
//#include "../sra-tools-gui/interfaces/ktoolbaritem.h"

#include <klib/rc.h>
#include <kfg/config.h>
#include <kfg/properties.h>
#include <kfg/repository.h>
#include <kfg/ngc.h>
#include <kfs/directory.h>
#include <kfs/file.h>

#include <QAction>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QCheckBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QToolBar>

#include <QDebug>

static vdbconf_model *config_model;

extern "C"
{
    rc_t run_interactive ( vdbconf_model &m )
    {
        config_model = &m;
        return 0;
    }
}

const QString rsrc_path = ":/images";

struct WorkspaceItem
{
    WorkspaceItem ( QString name, QString path, uint32_t id )
        : name_label ( new QLabel ( name ) )
        , path_label ( new QLabel ( path ) )
        , edit_button ( new QPushButton ( "Edit" ) )
        , ngc_id ( id )
    {
        name_label -> setFixedWidth ( 150 );
        name_label -> setAlignment ( Qt::AlignRight );

        path_label -> setFrameShape ( QFrame::Panel );
        path_label -> setFrameShadow ( QFrame::Sunken );

        edit_button -> setFixedSize ( 30, 20 );
    }

    WorkspaceItem ()
        : name_label ( nullptr )
        , path_label ( nullptr )
        , edit_button ( nullptr )
        , ngc_id ( -1 )
    {}

    QLabel *name_label;
    QLabel *path_label;
    QPushButton *edit_button;

    int ngc_id;
};


/* static functions */
static
bool location_error ( RootState state, QWidget *w )
{
    QString msg;

    switch ( state )
    {
    case RootState_NotChanged       : return true;
    case RootState_NotUnique        : msg = QString ( "location not unique, select a different one" ); break;
    case RootState_MkdirFail        : msg = QString ( "could not created directory, maybe permisson problem" ); break;
    case RootState_NewPathEmpty     : msg = QString ( "you gave me an empty path" ); break;
    case RootState_NewDirNotEmpty   : msg = QString ( "the given location is not empty" ); break;
    case RootState_NewNotDir        : msg = QString ( "new location is not a directory" ); break;
    case RootState_Error            : msg = QString ( "error changing location" ); break;
    default                             : msg = QString ( "unknown enum" ); break;
    }

    QMessageBox::critical ( w
                            , "Error"
                            , msg
                            , QMessageBox::Ok );
    return false;
}

static
std :: string public_location_start_dir ( SRAConfigModel *model )
{
    std :: string s = model -> get_public_path ();

    if ( ! model -> path_exists ( s ) )
        s = model -> get_user_default_path ();

    if ( ! model -> path_exists ( s ) )
        s = model -> get_home_path () + "/ncbi";

    if ( ! model -> path_exists ( s ) )
        s = model -> get_home_path ();

    if ( ! model -> path_exists ( s ) )
        s = model -> get_current_path ();

    return s;
}

static
bool select_public_location ( SRAConfigModel *model, QWidget *w )
{
    std ::string path = public_location_start_dir ( model ) . c_str ();

    if ( model -> path_exists ( path ) )
    {
        QString p = QFileDialog :: getOpenFileName ( w
                                                , "Import Workspace"
                                                , path . c_str () );
        path = p . toStdString ();
    }
    else
    {
        QString p = QInputDialog::getText ( w
                                       , ""
                                       , "Location of public cache"
                                       , QLineEdit::Normal );
        path = p . toStdString ();
    }

    if ( path . length () > 0 )
    {
        QString p = path . c_str ();
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question ( w
                                        , ""
                                        , "Change the location to '" + p + "'?"
                                        , QMessageBox::Yes | QMessageBox::No );
        if ( reply == QMessageBox::Yes )
        {
            bool flush_old = false;
            bool reuse_new = false;

            RootState state = model -> set_public_path ( path, flush_old, reuse_new );

            switch ( state )
            {
            case RootState_OK:
                return true;
            case RootState_OldNotEmpty:
            {
                QMessageBox::StandardButton reply;
                reply = QMessageBox::question ( w
                                                , "Directory not empty"
                                                , "Previous location is not empty, flush it?"
                                                , QMessageBox::Yes | QMessageBox::No );
                if ( reply == QMessageBox::Yes )
                {
                    flush_old = true;
                    state = model -> set_public_path ( path, flush_old, reuse_new );
                    if ( state == RootState_OK )
                        return true;
                    else
                        return location_error ( state, w );
        }
            }
            default:
                return location_error ( state, w );
            }
        }
    }

    return false;
}

static
std :: string protected_location_start_dir ( SRAConfigModel *model, uint32_t id )
{
    std :: string s = model -> get_workspace_path ( id );

    if ( ! model -> path_exists ( s ) )
        s = model -> get_user_default_path ();

    if ( ! model -> path_exists ( s ) )
        s = model -> get_home_path () + "/ncbi";

    if ( ! model -> path_exists ( s ) )
        s = model -> get_home_path ();

    if ( ! model -> path_exists ( s ) )
        s = model -> get_current_path ();

    return s;
}

static
bool select_protected_location ( SRAConfigModel *model, int id, QWidget *w )
{
    std :: string path = protected_location_start_dir ( model, id );
    qDebug () << "Protect location path: " << QString ( path . c_str () ) << " [" << id << "]";

    if ( model -> path_exists ( path ) )
    {
        QString p = QFileDialog :: getOpenFileName ( w
                                                , "Import Workspace"
                                                , path . c_str () );
        path = p . toStdString ();
    }
    else
    {
        QString p = QInputDialog::getText ( w
                                       , ""
                                       , "Location of dbGaP project"
                                       , QLineEdit::Normal );
        path = p . toStdString ();
    }

    if ( path . length () > 0 )
    {
        QString repo_name = model -> get_workspace_name ( id ) . c_str ();
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question ( w
                                        , ""
                                        , "Change the location of '" + repo_name + "' to '" + QString ( path . c_str () ) + "'?"
                                        , QMessageBox::Yes | QMessageBox::No );
        if ( reply == QMessageBox::Yes )
        {
            bool flush_old = false;
            bool reuse_new = false;

            RootState state = model -> set_workspace_path ( path, id, flush_old, reuse_new );

            switch ( state )
            {
            case RootState_OK:
                return true;
            case RootState_OldNotEmpty:
            {
                QMessageBox::StandardButton reply;
                reply = QMessageBox::question ( w
                                                , "Directory not empty"
                                                , "Previous location is not empty, flush it?"
                                                , QMessageBox::Yes | QMessageBox::No );
                if ( reply == QMessageBox::Yes )
                {
                    flush_old = true;
                    state = model -> set_workspace_path ( path, id, flush_old, reuse_new );
                    if ( state == RootState_OK )
                        return true;
                    else
                        return location_error ( state, w );
                }
            }
            default:
                return location_error ( state, w );
            }
        }
    }

    return false;
}

static
bool make_ngc_obj ( const KNgcObj ** ngc, std::string &path )
{
    KDirectory * dir;
    rc_t rc = KDirectoryNativeDir( &dir );
    if ( rc == 0 )
    {
        const KFile * src;
        rc = KDirectoryOpenFileRead ( dir, &src, "%s", path.c_str() );
        if ( rc == 0 )
        {
            rc = KNgcObjMakeFromFile ( ngc, src );
            KFileRelease( src );
        }
        KDirectoryRelease( dir );
    }

    return ( rc == 0 );
}

static
bool prepare_ngc ( SRAConfigModel *model, const KNgcObj *ngc, QString *loc, QWidget *w )
{
    std :: string base_path = model -> get_user_default_path ();
    std :: string path = model -> get_ngc_root ( base_path, ngc );

    RootState state = model -> configure_workspace ( path );

    switch ( state )
    {
    case RootState_OK:
    {
        *loc = path . c_str ();
        return true;
    }
    case RootState_OldNotEmpty:
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question ( w
                                        , "Directory not empty"
                                        , "Workspace location is not empty. Use it anyway?"
                                        , QMessageBox::Yes | QMessageBox::No );
        if ( reply == QMessageBox::Yes )
        {
            state = model -> configure_workspace ( path, true );
            if ( state == RootState_OK )
            {
                *loc = path . c_str ();
                return true;
            }
            else
                return location_error ( state, w );
        }
    }
    default:
        return location_error ( state, w );
    }

    return false;
}

static
bool import_ngc ( SRAConfigModel *model, std :: string file, uint32_t &ngc_id, QWidget *w )
{
   const KNgcObj *ngc;
   if ( ! make_ngc_obj ( &ngc, file ) )
   {
       QMessageBox::information ( w
                                  , "Import Error"
                                  , "Unable to import NGC file" );
   }
   else
   {
       QString path;
       if ( ! prepare_ngc ( model, ngc, &path, w ) )
           qDebug () << "failed to prepare ngc object";
       else
       {
           qDebug () << "prepared ngc object";

           uint32_t result_flags = 0;

           if ( model -> import_ngc ( path . toStdString (), ngc, INP_CREATE_REPOSITORY, &result_flags ) )
           {
               /* we have it imported or it exists and no changes made */
               bool modified = false;
               if ( result_flags & INP_CREATE_REPOSITORY )
               {
                   /* success is the most common outcome, the repository was created */
                   QMessageBox::information ( w
                                              , "Import Successful"
                                              , "project successfully imported" );

                   modified = true;
               }
               else
               {
                   /* repository did exist and is completely identical to the given ngc-obj */
                   QMessageBox::information ( w
                                              , ""
                                              , "this project exists already, no changes made" );

                   modified = false;
               }

               if ( model -> get_ngc_obj_id ( ngc, &ngc_id ) )
               {
                   qDebug () << "NGC ID: " << ngc_id;

                   QMessageBox::StandardButton reply;
                   reply = QMessageBox::question ( w
                                                   , ""
                                                   , "Do you want to change the location?"
                                                   , QMessageBox::Yes | QMessageBox::No );
                   if ( reply == QMessageBox::Yes )
                   {
                       modified |= select_protected_location ( model, ngc_id, w );
                   }
               }
               else
               {
                   QMessageBox::information ( w
                                              , ""
                                              , "Cannot find the imported Workspace" );
               }

               if ( modified )
               {
                   model -> commit (); // TBD - on import of NGC files, do we automaically commit, or allow for revert and require apply button?
                   model -> create_directory ( ngc );
                   return true;
               }
           }
           else if ( result_flags == 0 )
           {
               QMessageBox::critical ( w
                                       , "Error"
                                       , "Internal Error: Failed to impport the ngc-object"
                                       , QMessageBox::Ok );
           }
           else
           {
               QMessageBox::information ( w, "", "the repository does already exist!" );

               if ( result_flags & INP_UPDATE_ENC_KEY )
               {
                   QMessageBox::StandardButton reply;
                   reply = QMessageBox::question ( w
                                                   , ""
                                                   , "Encryption key would change, continue?"
                                                   , QMessageBox::Yes | QMessageBox::No );
                   if ( reply == QMessageBox::Yes  && ( result_flags & INP_UPDATE_DNLD_TICKET ) )
                   {
                       QMessageBox::StandardButton reply;
                       reply = QMessageBox::question ( w
                                                       , ""
                                                       , "Download ticket would change, continue?"
                                                       , QMessageBox::Yes | QMessageBox::No );
                       if ( reply == QMessageBox::Yes  && ( result_flags & INP_UPDATE_DESC ) )
                       {
                           QMessageBox::StandardButton reply;
                           reply = QMessageBox::question ( w
                                                           , ""
                                                           , "Description would change, continue?"
                                                           , QMessageBox::Yes | QMessageBox::No );
                           if ( reply == QMessageBox::Yes )
                           {
                               uint32_t result_flags2 = 0;
                               if ( model -> import_ngc ( path . toStdString () , ngc, result_flags, &result_flags2 ) )
                               {
                                   QMessageBox::StandardButton reply;
                                   reply = QMessageBox::question ( w
                                                                   , ""
                                                                   , "Project successfully updated!\nDo you want to change the location? "
                                                                   , QMessageBox::Yes | QMessageBox::No );

                                   if ( reply == QMessageBox::Yes )
                                   {
                                       /* we have to find out the id of the imported/existing repository */
                                       if ( model -> get_ngc_obj_id ( ngc, &ngc_id ) )
                                       {
                                           qDebug () << "NGC ID: " << ngc_id;
                                           select_protected_location ( model, ngc_id, w );
                                       }
                                       else
                                           QMessageBox::information ( w, "", "the repository does already exist!" );
                                   }

                                   model -> commit ();

                                   return true;
                               }
                               else
                               {
                                   QMessageBox::critical ( w
                                                           , "Error"
                                                           , "Internal Error: Failed to impport the ngc-object"
                                                           , QMessageBox::Ok );
                               }
                           }
                           else
                               QMessageBox::information ( w, "", "The import was canceled" );
                       }
                   }
               }
           }
       }

       KNgcObjRelease ( ngc );
   }

    return false;
}



/* Class methods */

SRAConfigView :: SRAConfigView ( QWidget *parent )
    : QWidget ( parent )
    , model ( new SRAConfigModel ( *config_model, this ) )
    , main_layout ( new QVBoxLayout () )
{
    //setup_menubar ();
    //setup_toolbar ();

    main_layout -> setSpacing ( 20 );
    main_layout -> setAlignment ( Qt::AlignTop );
    main_layout -> addSpacing ( 10 );
    main_layout -> addWidget ( setup_option_group () );
    main_layout -> addWidget ( setup_workspace_group () );
    main_layout -> addLayout ( setup_button_layout () );

    populate ();

    connect ( this, SIGNAL ( dirty_config () ), this, SLOT ( modified_config () ) );

    setLayout ( main_layout );

    resize ( size () );

    show ();
}

SRAConfigView :: ~SRAConfigView ()
{

}

/*
void SRAConfigView :: setup_menubar ()
{
    QMenu *file = menuBar () -> addMenu ( tr ( "&File" ) );

    apply_action = file -> addAction ( tr ( "&Apply" ), this, SLOT ( commit_config () ) );
    apply_action -> setDisabled ( true );

    file -> addSeparator ();
    file -> addAction ( tr ( "&Import" ), this, SLOT ( import_workspace () ) );

    QMenu *edit = menuBar () -> addMenu ( tr ( "\u200CEdit" ) ); // \u200C was added because OSX auto-adds some unwanted menu items

    discard_action = edit -> addAction ( tr ( "&Discard Changes" ), this, SLOT ( reload_config () ) );
    discard_action -> setDisabled ( true );

    edit -> addSeparator ();
    edit -> addAction ( tr ( "&Advanced" ), this, SLOT ( advanced_settings () ) );
    edit -> addSeparator ();
    edit -> addAction ( tr ( "&Soft Factory Reset" ), this, SLOT ( default_config () ) );
}

void SRAConfigView :: setup_toolbar ()
{
#ifdef Q_OS_OSX
    setUnifiedTitleAndToolBarOnMac ( true );
#endif

    QToolBar *bar = new QToolBar ( this );

    KToolbarItem *item = new KToolbarItem ( "General", rsrc_path + "/general_icon" );
    bar -> addWidget ( item );

    item = new KToolbarItem ( "AWS", rsrc_path + "/aws_icon" );
    bar -> addWidget ( item );

    item = new KToolbarItem ( "Network", rsrc_path + "/network_icon" );
    bar -> addWidget ( item );

    item = new KToolbarItem ( "Diagnostics", rsrc_path + "/troubleshooting" );
    bar -> addWidget ( item );

    addToolBar ( bar );

}
*/
void SRAConfigView :: populate ()
{
    remote_enabled_cb -> setChecked ( model -> remote_enabled () );

    local_caching_cb -> setChecked ( model -> global_cache_enabled () );

    if ( model -> site_workspace_exists () )
        site_cb -> setChecked ( model -> site_enabled () );
    else
        site_cb -> setDisabled ( true );

    proxy_cb -> setChecked ( model -> proxy_enabled () );
    proxy_label -> setText ( model -> get_proxy_path () . c_str () );

    http_priority_cb -> setChecked ( model -> proxy_priority () );
}

QGroupBox * SRAConfigView::setup_option_group ()
{
    QGroupBox *group = new QGroupBox ( "Options" );
    group -> setFixedHeight ( 170 );

    QGridLayout *layout = new QGridLayout ();
    layout -> setAlignment ( Qt :: AlignTop );
    layout -> setSpacing ( 15 );

    //1
    remote_enabled_cb = new QCheckBox ( "Enable Remote Access" );
    remote_enabled_cb -> setAutoExclusive ( false );
    layout -> addWidget ( remote_enabled_cb, 0, 0 );
    connect ( remote_enabled_cb, SIGNAL ( clicked ( bool ) ), this, SLOT ( toggle_remote_enabled ( bool ) ) );

    //2
    local_caching_cb = new QCheckBox ( "Enable Local File Caching" );
    local_caching_cb -> setAutoExclusive ( false );
    layout -> addWidget ( local_caching_cb, 1, 0 );
     connect ( local_caching_cb, SIGNAL ( clicked ( bool ) ), this, SLOT ( toggle_local_caching ( bool ) ) );

    //3
    site_cb = new QCheckBox ( "Use Site Installation" );
    site_cb -> setAutoExclusive ( false );
    layout -> addWidget ( site_cb, 2, 0 );
    connect ( site_cb, SIGNAL ( clicked ( bool ) ), this, SLOT ( toggle_use_site ( bool ) ) );

    //4
    proxy_cb = new QCheckBox ( "Use Proxy" );
    proxy_cb -> setAutoExclusive ( false );
    layout -> addWidget ( proxy_cb, 3, 0 );
    connect ( proxy_cb, SIGNAL ( clicked ( bool ) ), this, SLOT ( toggle_use_proxy ( bool ) ) );

    proxy_label = new QLabel ();
    proxy_label -> setMargin ( 0 );
    proxy_label -> setFrameShape ( QFrame::Panel );
    proxy_label -> setFrameShadow ( QFrame::Sunken );
    proxy_label -> setFixedHeight ( 20 );
    layout -> addWidget ( proxy_label, 3, 1 );

    QPushButton *edit = new QPushButton ( "Edit" );
    edit -> setFixedSize ( 30, 20 );
    layout -> addWidget ( edit, 3, 2 );
    connect ( edit, SIGNAL ( clicked () ), this, SLOT ( edit_proxy_path () ) );

    //5
    http_priority_cb = new QCheckBox ( "Prioritize Environment Variable 'http-proxy'" );
    http_priority_cb -> setAutoExclusive ( false );
    layout -> addWidget ( http_priority_cb, 4, 0 );
    connect ( http_priority_cb, SIGNAL ( clicked ( bool ) ), this, SLOT ( toggle_prioritize_http ( bool ) ) );

    group -> setLayout ( layout );

    return group;
}

void SRAConfigView :: add_workspace (QString name, QString val, int ngc_id, bool insert )
{
    QHBoxLayout *layout = new QHBoxLayout ();

    WorkspaceItem *ws = new WorkspaceItem ( name . append ( ':' ), val, ngc_id );

    if ( ngc_id == -1 )
    {
        public_workspace = ws;
        connect ( ws -> edit_button, SIGNAL ( clicked () ), this, SLOT ( edit_public_path () ) );
    }
    else
    {
        protected_workspaces . append ( ws );
        connect ( ws -> edit_button, SIGNAL ( clicked () ), this, SLOT ( edit_workspace_path () ) );
    }

    layout -> addWidget ( ws -> name_label );
    layout -> addWidget ( ws -> path_label );
    layout -> addWidget ( ws -> edit_button );

    if ( insert )
    {
        workspace_layout -> insertLayout ( workspace_layout -> count () - 1, layout );
    }
    else
        workspace_layout -> addLayout ( layout );
}


void SRAConfigView :: import_workspace ()
{
    // open a file dialog to browse for the repository
    std :: string path = model -> get_home_path ();
    if ( ! model -> path_exists ( path ) )
        path = model -> get_current_path ();

    QString filter = tr ("NGS (*.ngc)" );
    QString file = QFileDialog :: getOpenFileName ( this
                                                    , "Import Workspace"
                                                    , path . c_str ()
                                                    , tr ("NGC files (*.ngc)" ) );

    if ( ! file . isEmpty () )
    {
        std :: string s = file . toStdString ();
        uint32_t ngc_id;
        if ( import_ngc ( model, s, ngc_id, this ) )
        {
            QString name = model -> get_workspace_name ( ngc_id ) . c_str ();

            name = QInputDialog::getText ( this
                                                   , tr ( "Name Workspace" )
                                                   , tr ( "Choose a name for your workspace" )
                                                   , QLineEdit::Normal
                                                   , name );

            if ( name . isEmpty () )
                name = model -> get_workspace_name ( ngc_id ) . c_str ();

            add_workspace ( name, file, ngc_id, true );
        }
    }
}


QGroupBox * SRAConfigView :: setup_workspace_group ()
{
    QGroupBox *group = new QGroupBox ( "Workspaces" );

    workspace_layout = new QVBoxLayout ();
    workspace_layout -> setAlignment ( Qt :: AlignTop );
    workspace_layout -> setSpacing ( 15 );

    add_workspace ( "Public", model -> get_public_path () . c_str(), -1 );

    int ws_count = model -> workspace_count ();
    qDebug () << "Setup workspace group: repo-count: " << ws_count;
    for ( int i = 0; i < ws_count; ++ i )
    {
        std :: string name = model -> get_workspace_name ( i );
        add_workspace ( name . c_str () ,
                        model -> get_workspace_path ( i ) . c_str (),
                        model -> get_workspace_id ( name ) );
    }

    //3
    QHBoxLayout *i_layout = new QHBoxLayout ();

    i_layout -> addSpacing ( 125 );

    QPushButton *import = new QPushButton ( "+" );
    import -> setFixedSize ( 30, 25 );
    connect ( import, SIGNAL ( clicked () ), this, SLOT ( import_workspace () ) );
    i_layout -> addWidget ( import );

    i_layout -> addSpacing ( 5 );

    QLabel *label = new QLabel ();
    label -> setFrameShape ( QFrame::Panel );
    label -> setFrameShadow ( QFrame::Sunken );

    i_layout -> addWidget ( label );

    workspace_layout -> addLayout ( i_layout );

    group -> setLayout ( workspace_layout );

    return group;
}

QVBoxLayout * SRAConfigView::setup_button_layout ()
{
    QVBoxLayout *v_layout = new QVBoxLayout ();

    // 1
    QHBoxLayout *layout = new QHBoxLayout ();
    layout -> setAlignment ( Qt::AlignTop | Qt::AlignRight );

    QPushButton *advanced = new QPushButton ( "Advanced" );
    advanced -> setFixedWidth ( 150 );
    connect ( advanced, SIGNAL ( clicked () ), this, SLOT ( advanced_settings () ) );

    layout -> addWidget ( advanced );
    v_layout -> addLayout ( layout );
    v_layout -> addStretch ( 1 );

    // 2
    layout = new QHBoxLayout ();
    layout -> setAlignment ( Qt::AlignBottom | Qt::AlignRight );
    layout -> setSpacing ( 5 );

    apply_btn = new QPushButton ( "Apply" );
    apply_btn -> setDisabled ( true );
    connect ( apply_btn, SIGNAL ( clicked () ), this, SLOT ( commit_config  () ) );

    discard_btn = new QPushButton ( "Revert" );
    discard_btn -> setDisabled ( true );
    connect ( discard_btn, SIGNAL ( clicked () ), this, SLOT ( reload_config () ) );

    layout -> addWidget ( discard_btn );
    layout -> addWidget ( apply_btn );

    v_layout -> addLayout ( layout );

    return v_layout;
}

void SRAConfigView :: closeEvent ( QCloseEvent *ev )
{
    if ( model -> config_changed () )
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question ( this
                                        , ""
                                        , "Save changes? "
                                        , QMessageBox::No | QMessageBox::Yes );

        if ( reply == QMessageBox::Yes )
            commit_config ();
    }

    ev -> accept ();
}

void SRAConfigView :: advanced_settings ()
{
    adv_setting_window = new QFrame ();
    adv_setting_window -> resize ( this -> width () * .7, this -> height () / 2 );
    adv_setting_window -> setWindowTitle ( "Advanced Settings" );

    QVBoxLayout *v_layout = new QVBoxLayout ();

    // 1
    QHBoxLayout *layout = new QHBoxLayout ();

    QLabel *label = new QLabel ( "Default import path:" );
    label -> setFixedWidth ( 150 );
    label -> setAlignment ( Qt::AlignRight );
    layout -> addWidget ( label);

    import_path_label = new QLabel ( model -> get_user_default_path () . c_str () );
    import_path_label -> setFrameShape ( QFrame::Panel );
    import_path_label -> setFrameShadow ( QFrame::Sunken );
    layout -> addWidget ( import_path_label );

    QPushButton *edit = new QPushButton ( "Edit" );
    connect ( edit, SIGNAL ( clicked () ), this, SLOT ( edit_import_path () ) );
    edit -> setFixedSize ( 30, 20 );
    layout -> addWidget ( edit );

    v_layout -> addLayout ( layout );
    v_layout -> addStretch ( 1 );

    // last
    layout = new QHBoxLayout ();
    layout -> setAlignment ( Qt::AlignBottom | Qt::AlignRight );

    QPushButton *done = new QPushButton ( "Done" );
    connect ( done, SIGNAL ( clicked () ), adv_setting_window, SLOT ( close () ) );

    layout -> addWidget ( done );
    v_layout -> addLayout ( layout );

    adv_setting_window -> setLayout ( v_layout );

    adv_setting_window -> show ();
}

void SRAConfigView :: commit_config ()
{
    if ( ! model -> commit () )
        QMessageBox::information ( this, "", "Error saving changes" );

    apply_btn -> setDisabled ( true );
    discard_btn -> setDisabled ( true );
}

void SRAConfigView :: reload_config ()
{
    model -> reload ();
    populate ();

   if ( ! model -> config_changed () )
   {
       apply_btn -> setDisabled ( true );
       apply_action -> setDisabled ( true );
       discard_btn -> setDisabled ( true );
       discard_action -> setDisabled ( true );
   }
}

void SRAConfigView :: modified_config ()
{
    if ( model -> config_changed () ) // this wont trigger on workspace addition yet
    {
        apply_btn -> setDisabled ( false );
        apply_action -> setDisabled ( false );
        discard_btn -> setDisabled ( false );
        discard_action -> setDisabled ( false );
    }
}

// TBD - still needs a menu item to be triggered. -- this is not a hard reset - it still keeps some user settings
void SRAConfigView :: default_config ()
{
    model -> set_remote_enabled ( true );
    model -> set_global_cache_enabled ( true );
    model -> set_site_enabled ( true );

    populate ();

    emit dirty_config ();
}

void SRAConfigView :: toggle_remote_enabled ( bool toggled )
{
    model -> set_remote_enabled ( toggled );
    emit dirty_config ();
}

void SRAConfigView :: toggle_local_caching ( bool toggled )
{
    model -> set_global_cache_enabled ( toggled );
    emit dirty_config ();
}

void SRAConfigView :: toggle_use_site ( bool toggled )
{
    model -> set_site_enabled ( toggled );
    emit dirty_config ();
}

void SRAConfigView :: toggle_use_proxy ( bool toggled )
{
    model -> set_proxy_enabled ( toggled );
    emit dirty_config ();
}

void SRAConfigView :: toggle_prioritize_http ( bool toggled )
{
    model -> set_proxy_priority ( toggled );
    emit dirty_config ();
}

void SRAConfigView :: edit_import_path ()
{
    std :: string path = model -> get_user_default_path () . c_str ();

    if ( ! model -> path_exists ( path ) )
        path = model -> get_home_path ();

    if ( ! model -> path_exists ( path ) )
        path = model -> get_current_path ();

    QString e_path = QFileDialog :: getOpenFileName ( adv_setting_window
                                                    , ""
                                                    , path . c_str () );


    if ( e_path . isEmpty () )
        return;

    import_path_label -> setText ( e_path );
    model -> set_user_default_path ( e_path . toStdString () . c_str () );

    emit dirty_config ();
}

void SRAConfigView :: edit_proxy_path ()
{
    QString input = QInputDialog::getText ( this
                                                , tr ( "Proxy Path" )
                                                , tr ( "Enter a proxy path" )
                                                , QLineEdit::Normal
                                                , proxy_label -> text () );

    if ( input . isEmpty () )
        return;

    proxy_label -> setText ( input );
    model -> set_proxy_path ( input . toStdString () );

    emit dirty_config ();
}

void SRAConfigView :: edit_public_path ()
{
    qDebug () << public_workspace -> ngc_id;
    if ( select_public_location ( model, this ) )
    {
        public_workspace -> path_label -> setText ( model -> get_public_path () . c_str () );

        emit dirty_config ();
    }
}

void SRAConfigView :: edit_workspace_path ()
{
    foreach ( WorkspaceItem *item,  protected_workspaces )
    {
        if ( sender () == item -> edit_button )
        {
            qDebug () << item -> ngc_id;

            if ( select_protected_location ( model, item -> ngc_id, this ) )
            {
                QString path =  model -> get_workspace_path ( item -> ngc_id ) . c_str ();
                item -> path_label -> setText ( path );
                import_path_label -> setText ( path );

                emit dirty_config ();
            }

            return;
        }
    }
}


