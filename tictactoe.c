#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <termios.h>
#include <unistd.h>

typedef enum { unclaimed, x_space, o_space } tile_type;
typedef enum { up, down, left, right, confirm, invalid } cursor_move_dir;

typedef enum { none, draw, x, o } winning_player;
typedef enum { horizontal, vertical, diagonal } win_type;

typedef struct winner {
  winning_player player;
  win_type kind;
  unsigned long index;
} winner;

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_RESET "\x1b[0m"

char getch() {
  char buf = 0;
  struct termios old = {0};
  if (tcgetattr(0, &old) < 0)
    perror("tcsetattr()");
  old.c_lflag &= ~ICANON;
  old.c_lflag &= ~ECHO;
  old.c_cc[VMIN] = 1;
  old.c_cc[VTIME] = 0;
  if (tcsetattr(0, TCSANOW, &old) < 0)
    perror("tcsetattr ICANON");
  if (read(0, &buf, 1) < 0)
    perror("read()");
  old.c_lflag |= ICANON;
  old.c_lflag |= ECHO;
  if (tcsetattr(0, TCSADRAIN, &old) < 0)
    perror("tcsetattr ~ICANON");
  return (buf);
}

typedef struct board {
  unsigned char *tiles;
  unsigned long tile_size;
  unsigned int rows;
  unsigned int columns;
} board;

typedef struct cursor {
  unsigned int row;
  unsigned int column;
} cursor;

static board *make_board(unsigned int rows, unsigned int columns) {
  board *b = (board *)malloc(sizeof(board));
  b->rows = rows;
  b->columns = columns;
  b->tile_size = (unsigned long)rows * columns;
  fprintf(stderr, "Creating board (%u rows, %u columns, %lu tiles)\n", rows,
          columns, b->tile_size);
  b->tiles = (unsigned char *)malloc(b->tile_size * sizeof(unsigned char));
  for (unsigned long size = 0; size < b->tile_size; size += 1)
    b->tiles[size] = unclaimed;
  return b;
}

static void free_board(board *b) {
  free(b->tiles);
  free(b);
}

static unsigned long to_index(board *b, unsigned int row, unsigned int column) {
  return (b->rows * row) + column;
}

static unsigned char *piece_at(board *b, unsigned int row,
                               unsigned int column) {
  return &b->tiles[to_index(b, row, column)];
}

static void draw_board(board *b, cursor *c, bool *is_on_first_player,
                       winner *maybe_winner) {
  if (is_on_first_player != NULL)
    printf("%s%s%s's turn\n",
           *is_on_first_player ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,
           *is_on_first_player ? "X" : "O", ANSI_COLOR_RESET);
  else if (maybe_winner->player != draw)
    printf("%s won! (index=%lu)\n", maybe_winner->player == x ? "X" : "O",
           maybe_winner->index);
  else
    printf("Draw!");

  unsigned long visited = 0;
  unsigned long cursor_index = c == NULL ? 0 : to_index(b, c->row, c->column);
  for (unsigned int row = 0; row < b->rows; row += 1) {
    bool is_last_row = row == b->rows - 1;
    bool is_winning_row = maybe_winner != NULL &&
                          maybe_winner->kind == horizontal &&
                          maybe_winner->index == row;
    for (unsigned int column = 0; column < b->columns; column += 1) {
      bool is_winning_column = maybe_winner != NULL &&
                               maybe_winner->kind == vertical &&
                               maybe_winner->index == column;
      unsigned char *piece = piece_at(b, row, column);
      bool is_last_column = column == b->columns - 1;
      bool is_selected = c != NULL && cursor_index == visited;
      if (is_selected && is_on_first_player != NULL)
        printf(*is_on_first_player ? ANSI_COLOR_GREEN : ANSI_COLOR_RED);
      if (is_winning_row || is_winning_column)
        printf(maybe_winner->player == x ? ANSI_COLOR_GREEN : ANSI_COLOR_RED);
      if (is_selected && *piece == unclaimed)
        printf("?");
      else if (*piece == unclaimed)
        printf(" ");
      else
        printf("%s", *piece == x_space ? "x" : "o");
      if (is_selected || is_winning_row || is_winning_column)
        printf(ANSI_COLOR_RESET);
      if (!is_last_column) {
        printf("|");
      }
      visited += 1;
    }
    puts("");
    if (!is_last_row) {
      for (unsigned int column = 0; column < (b->columns * 2) - 1;
           column += 1) {
        printf("%s", column % 2 == 0 ? "-" : "+");
      }
      puts("");
    }
  }
}

static void reset_screen_for_board(board *b) {
  // jump up one line, jump to beginning, clear line
  for (unsigned int row = 0; row < b->rows; row += 1)
    printf("\033[F\033[A\033[K");
}

static cursor_move_dir read_input(void) {
  char first = getch();
  if (first == 10)
    return confirm;
  if (first == 27 && getch() == 91) {
    switch (getch()) {
    case 68:
      return left;
    case 67:
      return right;
    case 65:
      return up;
    case 66:
      return down;
    }
  }
  return invalid;
}

// `move_cursor_in_direction` will attempt to move the cursor in the specified
// arrow direction the cursor will not wrap, or move off of the grid
//
// this function will do nothing when called with move as invalid or confirm, as
// those are handled separately by the parent
//
// TODO(haze): fix cursor movement bug when you have uneven row/col
static void move_cursor_in_direction(board *b, cursor *c,
                                     cursor_move_dir move) {
  switch (move) {
  case up:
    if (c->row > 0)
      c->row -= 1;
    break;
  case down:
    if (c->row + 1 < b->rows)
      c->row += 1;
    break;
  case left:
    // if we can move left
    if (c->column > 0)
      c->column -= 1;
    break;
  case right:
    // if we can move right
    if (c->column + 1 < b->columns)
      c->column += 1;
    break;
  case invalid:
  case confirm:
    // do nothing
    break;
  }
}

bool check_vertical_win(board *b, winner *winner) {
  fputs("checking\n", stderr);
  for (unsigned int column = 0; column < b->columns; column += 1) {
    tile_type first_tile = b->tiles[column];
    bool win = true;
    if (first_tile != unclaimed) {
      unsigned int row;
      unsigned long end_idx = column + (b->rows * (b->columns - 1));
      for (row = column + b->rows; row <= end_idx; row += b->rows) {
        fprintf(stderr, "row=%d\n", row);
        tile_type tile = b->tiles[row];
        win = win && (tile == first_tile);
        if (tile == unclaimed) {
          fputs("failed unclaim\n", stderr);
          break;
        }
        if (!win) {
          fputs("failed win\n", stderr);
          break;
        }
      }
      fprintf(stderr, "win=%d, row=%d, end=%lu\n", win, row, end_idx);
      if (win && (row - b->rows) == end_idx) {
        winner->player = first_tile == x_space ? x : o;
        winner->index = column;
        winner->kind = vertical;
        return true;
      }
    }
  }
  return false;
}

bool check_horizontal_win(board *b, winner *winner) {
  for (unsigned int row = 0; row < b->rows; row += 1) {
    unsigned long offset = b->rows * row;
    tile_type first_tile = b->tiles[offset];
    bool win = true;
    if (first_tile != unclaimed) {
      unsigned int column;
      for (column = offset + 1; column <= offset + b->columns; column += 1) {
        tile_type tile = b->tiles[column];
        if (tile == unclaimed)
          break;
        win = win && (tile == first_tile);
        if (!win)
          break;
      }
      if (column == offset + b->columns) {
        winner->player = first_tile == x_space ? x : o;
        winner->index = row;
        winner->kind = horizontal;
        return true;
      }
    }
  }
  return false;
}

// Code duplication is based :^)
// TODO(haze): deduplicate using translation later
void check_win(board *b, winner *winner) {
  // check for horizontal wins
  if (check_horizontal_win(b, winner))
    return;
  if (check_vertical_win(b, winner))
    return;
}

static void loop(board *b) {
  cursor *c = malloc(sizeof(cursor));
  bool is_on_first_player = true;

  winner *maybe_win = (winner *)malloc(sizeof(winner));

  maybe_win->player = none;
  maybe_win->index = 0;

  c->column = 0;
  c->row = 0;

  while (1) {
    // draw
    draw_board(b, c, &is_on_first_player, NULL);
    // input
    cursor_move_dir attempted_move = read_input();
    if (attempted_move == confirm) {
      *piece_at(b, c->row, c->column) = is_on_first_player ? x_space : o_space;
      check_win(b, maybe_win);
      if (maybe_win->player != none) {
        break;
      }
      is_on_first_player = !is_on_first_player;
    } else if (attempted_move != invalid) {
      move_cursor_in_direction(b, c, attempted_move);
    }
    reset_screen_for_board(b);
  }

  reset_screen_for_board(b);
  draw_board(b, c, NULL, maybe_win);

  free(maybe_win);
  free(c);
}

int main(void) {
  board *b = make_board(10, 10);
  loop(b);
  free_board(b);
  return EXIT_SUCCESS;
}
