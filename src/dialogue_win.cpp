#include "dialogue_win.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "catacharset.h"
#include "input.h"
#include "messages.h"
#include "output.h"
#include "point.h"
#include "sdltiles.h"
#include "string_formatter.h"
#include "translations.h"
#include "ui_manager.h"

// Height of the response section
static const int RESPONSES_LINES = 15;

multiline_list_entry talk_data::get_entry() const
{
    multiline_list_entry entry;
    entry.entry_text = colorize( text, color );
    entry.prefix = formatted_hotkey( hotkey_desc, color );
    return entry;
}

dialogue_window::dialogue_window()
{
    responses_list = std::make_unique<multiline_list>( resp_win );
    history_view = std::make_unique<scrolling_text_view>( history_win );
}

bool dialogue_window::has_character_sidebar() const
{
    return !is_computer && !is_not_conversation;
}

void dialogue_window::resize( ui_adaptor &ui )
{
    const int win_beginy = TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 4 : 0;
    const int win_beginx = TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 4 : 0;
    const int maxy = win_beginy ? TERMY - 2 * win_beginy : FULL_SCREEN_HEIGHT;
    const int maxx = win_beginx ? TERMX - 2 * win_beginx : FULL_SCREEN_WIDTH;
    d_win = catacurses::newwin( maxy, maxx, point( win_beginx, win_beginy ) );
    ui.position_from_window( d_win );

    sidebar_width = 0;
    content_left = 0;
    content_width = maxx - 1;
    portrait_height_cells = 0;
    portrait_win = catacurses::window();

    if( has_character_sidebar() ) {
        // The character card owns roughly one fifth of the dialogue width.  Keep enough room on
        // small terminals for the actual conversation to remain readable.
        sidebar_width = std::max( 16, maxx / 5 );
        sidebar_width = std::min( sidebar_width, std::max( 0, maxx - 40 ) );
        content_left = sidebar_width + 1;
        content_width = std::max( 1, maxx - content_left - 1 );

        const int portrait_cols = std::max( 8, sidebar_width - 2 );
        const int inner_width_px = std::max( 1, ( portrait_cols - 2 ) * fontwidth );
        const int desired_inner_height_px = inner_width_px * 4 / 3;
        const int max_portrait_rows = std::max( 6, maxy - 10 );
        portrait_height_cells = std::clamp(
                                    ( desired_inner_height_px + fontheight - 1 ) / fontheight + 2,
                                    6, max_portrait_rows );

        const point portrait_begin( win_beginx + 1, win_beginy + 2 );
        if( image ) {
            const int inner_height_px = std::max( 1, ( portrait_height_cells - 2 ) * fontheight );
            int texture_width = 3;
            int texture_height = 4;
            if( SDL_QueryTexture( image, nullptr, nullptr, &texture_width, &texture_height ) != 0 ||
                texture_width <= 0 || texture_height <= 0 ) {
                texture_width = 3;
                texture_height = 4;
            }

            const double scale = std::min(
                                     static_cast<double>( inner_width_px ) / texture_width,
                                     static_cast<double>( inner_height_px ) / texture_height );
            image_width = std::max( 1, static_cast<int>( std::lround( texture_width * scale ) ) );
            image_height = std::max( 1, static_cast<int>( std::lround( texture_height * scale ) ) );

            const int inner_x = ( portrait_begin.x + 1 ) * fontwidth;
            const int inner_y = ( portrait_begin.y + 1 ) * fontheight;
            rect_2.x = inner_x + ( inner_width_px - image_width ) / 2;
            rect_2.y = inner_y + ( inner_height_px - image_height ) / 2;
            rect_2.w = image_width;
            rect_2.h = image_height;

            portrait_win = catacurses::newwin( portrait_height_cells, portrait_cols, portrait_begin,
                                               image, image_width, image_height, rect_2 );
        } else {
            image_width = 0;
            image_height = 0;
            portrait_win = catacurses::newwin( portrait_height_cells, portrait_cols, portrait_begin );
        }
    }

    history_win = catacurses::newwin( maxy - 1 - RESPONSES_LINES - 2 - 1, content_width,
                                      point( win_beginx + content_left, win_beginy + 2 ) );

    // Dialogue choices keep most of the lower pane.  The remaining third is reserved for the
    // fixed utility actions such as look, size up and yell.
    response_width = has_character_sidebar() ? std::max( 1, content_width * 2 / 3 ) :
                     std::max( 1, maxx / 2 );
    resp_win = catacurses::newwin( RESPONSES_LINES - 1, response_width,
                                   point( win_beginx + content_left,
                                          win_beginy + maxy - RESPONSES_LINES ) );

    // Reset size-dependant state
    update_history_view = true;
    responses_list->fold_entries();
}

void dialogue_window::draw_character_sidebar( const std::string &npc_name )
{
    if( !has_character_sidebar() || sidebar_width <= 0 ) {
        return;
    }

    if( portrait_win ) {
        werase( portrait_win );
        draw_border( portrait_win );
        if( !image ) {
            center_print( portrait_win, getmaxy( portrait_win ) / 2, c_dark_gray, _( "暂无立绘" ) );
        }
    }

    int text_y = 2 + portrait_height_cells + 1;
    const int text_width = std::max( 1, sidebar_width - 4 );
    if( text_y < getmaxy( d_win ) - 1 ) {
        trim_and_print( d_win, point( 2, text_y ), text_width, c_white, npc_name );
        ++text_y;
    }
    if( !character_profession.empty() && text_y < getmaxy( d_win ) - 1 ) {
        trim_and_print( d_win, point( 2, text_y ), text_width, c_light_gray,
                        string_format( _( "身份，%s" ), character_profession ) );
        ++text_y;
    }
    if( !relationship_text.empty() && text_y < getmaxy( d_win ) - 1 ) {
        trim_and_print( d_win, point( 2, text_y ), text_width, c_light_cyan,
                        string_format( _( "关系，%s" ), relationship_text ) );
    }
}

void dialogue_window::draw( const std::string &npc_name )
{
    werase( d_win );

    print_header( npc_name );
    draw_character_sidebar( npc_name );

    int ycurrent = getmaxy( d_win ) - 1 - RESPONSES_LINES + 1;
    // Actions go on the right side of the response pane; they're unaffected by scrolling.
    input_context ctxt( "DIALOGUE_CHOOSE_RESPONSE" );
    if( !is_computer && !is_not_conversation ) {
        const int actions_xoffset = std::min( getmaxx( d_win ) - 2,
                                              content_left + response_width + 2 );
        nc_color cur_color = c_magenta;
        std::string formatted_text = formatted_hotkey( ctxt.get_desc( "LOOK_AT", 1 ),
                                     cur_color ).append( _( "Look at" ) );
        print_colored_text( d_win, point( actions_xoffset, ycurrent ), cur_color, c_magenta,
                            formatted_text );
        ++ycurrent;
        formatted_text = formatted_hotkey( ctxt.get_desc( "SIZE_UP_STATS", 1 ),
                                           cur_color ).append( _( "Size up stats" ) );
        print_colored_text( d_win, point( actions_xoffset, ycurrent ), cur_color, c_magenta,
                            formatted_text );
        ++ycurrent;
        formatted_text = formatted_hotkey( ctxt.get_desc( "YELL", 1 ), cur_color ).append( _( "Yell" ) );
        print_colored_text( d_win, point( actions_xoffset, ycurrent ), cur_color, c_magenta,
                            formatted_text );
        ++ycurrent;
        formatted_text = formatted_hotkey( ctxt.get_desc( "CHECK_OPINION", 1 ),
                                           cur_color ).append( _( "Check opinion" ) );
        print_colored_text( d_win, point( actions_xoffset, ycurrent ), cur_color, c_magenta,
                            formatted_text );
    }
    wnoutrefresh( d_win );
    if( portrait_win ) {
        wnoutrefresh( portrait_win );
    }

    responses_list->print_entries();

    if( update_history_view ) {
        update_history_view = false;
        const int newindex = history.size() - num_lines_highlighted;
        std::string assembled;
        for( int i = 0; i < static_cast<int>( history.size() ); ++i ) {
            nc_color col = ( i >= newindex ) ? history[i].color : c_light_gray;
            assembled += colorize( history[i].text, col ).append( "\n" );
        }

        history_view->set_text( assembled, false );
    }
    history_view->draw( c_light_gray );
}

void dialogue_window::handle_scrolling( std::string &action, input_context &ctxt )
{
    if( responses_list->handle_navigation( action, ctxt ) ||
        history_view->handle_navigation( action, ctxt ) ) {
        // No further action required
    }
    sel_response = responses_list->get_entry_pos();
}

void dialogue_window::set_up_scrolling( input_context &ctxt ) const
{
    if( !is_computer && !is_not_conversation ) {
        ctxt.register_action( "LOOK_AT" );
        ctxt.register_action( "SIZE_UP_STATS" );
        ctxt.register_action( "YELL" );
        ctxt.register_action( "CHECK_OPINION" );
    }
    history_view->set_up_navigation( ctxt, scrolling_key_scheme::angle_bracket_scroll );
    responses_list->set_up_navigation( ctxt );
}

void dialogue_window::add_to_history( const std::string &text, const std::string &speaker_name,
                                      nc_color speaker_color )
{
    add_to_history( speaker_name, speaker_color );
    add_to_history( text );
}

void dialogue_window::add_to_history( const std::string &text )
{
    add_to_history( text, default_color() );
}

void dialogue_window::add_to_history( const std::string &text, nc_color color )
{
    history.emplace_back( color, text );
    ++num_lines_highlighted;
    update_history_view = true;
}

void dialogue_window::add_history_separator()
{
    add_to_history( "", default_color() );
}

void dialogue_window::clear_history_highlights()
{
    num_lines_highlighted = 0;
}

nc_color dialogue_window::default_color() const
{
    return is_computer ? c_green : c_white;
}

void dialogue_window::print_header( const std::string &name ) const
{
    draw_border( d_win );
    if( sidebar_width > 0 ) {
        mvwvline( d_win, point( sidebar_width, 1 ), LINE_XOXO, getmaxy( d_win ) - 2 );
    }

    const int header_x = content_left + 2;
    if( is_computer ) {
        mvwprintz( d_win, point( header_x, 1 ), default_color(), _( "Interaction: %s" ), name );
    } else if( !is_not_conversation ) {
        mvwprintz( d_win, point( header_x, 1 ), default_color(), _( "Dialogue: %s" ), name );
    }
    const int xmax = getmaxx( d_win );
    const int ymax = getmaxy( d_win );
    const int ybar = ymax - 1 - RESPONSES_LINES - 1;
    // Horizontal bar dividing history and responses.  Keep the character card continuous.
    if( sidebar_width > 0 ) {
        mvwputch( d_win, point( sidebar_width, ybar ), BORDER_COLOR, LINE_XXXX );
        mvwhline( d_win, point( sidebar_width + 1, ybar ), LINE_OXOX,
                   xmax - sidebar_width - 2 );
    } else {
        mvwputch( d_win, point( 0, ybar ), BORDER_COLOR, LINE_XXXO );
        mvwhline( d_win, point( 1, ybar ), LINE_OXOX, xmax - 1 );
    }
    mvwputch( d_win, point( xmax - 1, ybar ), BORDER_COLOR, LINE_XOXX );
    if( is_computer ) {
        // NOLINTNEXTLINE(cata-use-named-point-constants)
        mvwprintz( d_win, point( content_left + 2, ybar + 1 ), default_color(), _( "Your input:" ) );
    } else if( is_not_conversation ) {
        mvwprintz( d_win, point( content_left + 2, ybar + 1 ), default_color(), _( "What do you do?" ) );
    } else {
        // NOLINTNEXTLINE(cata-use-named-point-constants)
        mvwprintz( d_win, point( content_left + 2, ybar + 1 ), default_color(), _( "Your response:" ) );
    }
}

void dialogue_window::set_responses( const std::vector<talk_data> &responses )
{
    responses_list->create_entries( responses );
}

catacurses::window *dialogue_window::get_d_win()
{
    return &d_win;
}

catacurses::window *dialogue_window::get_history_win()
{
    return &history_win;
}

catacurses::window *dialogue_window::get_resp_win()
{
    return &resp_win;
}

void dialogue_window::set_image( SDL_Texture *image )
{
    this->image = image;
}

void dialogue_window::set_character_profession( const std::string &profession )
{
    character_profession = profession;
}

void dialogue_window::set_relationship( const std::string &relationship )
{
    relationship_text = relationship;
}
