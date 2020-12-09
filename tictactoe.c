#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <termios.h>
#include <unistd.h>

typedef enum { unclaimed, x_space, o_space } tile_type;
typedef enum { up, down, left, right, invalid } cursor_move_dir;

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
  return (b->columns * column) + row;
}

static unsigned char *piece_at(board *b, unsigned int row,
                               unsigned int column) {
  return &b->tiles[to_index(b, row, column)];
}

static void draw_board(board *b, cursor *c) {
  unsigned long visited = 0;
  unsigned long cursor_index = c == NULL ? 0 : to_index(b, c->row, c->column);
  for (unsigned int row = 0; row < b->rows; row += 1) {
    bool is_last_row = row == b->rows - 1;
    for (unsigned int column = 0; column < b->columns; column += 1) {
      unsigned char *piece = piece_at(b, row, column);
      bool is_last_column = column == b->columns - 1;
      bool is_selected = c != NULL && cursor_index == visited;
      if (is_selected)
        printf(ANSI_COLOR_GREEN);
      printf("%d", *piece);
      if (is_selected)
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
  puts("");
  for (unsigned int row = 0; row < b->rows; row += 1)
    printf("\033[F\033[A");
}

static cursor_move_dir read_cursor_movement() {
  if (getch() == 27 && getch() == 91) {
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

static void move_cursor_in_direction(board *b, cursor *c,
                                     cursor_move_dir move) {
  switch (move) {
  case up:
    if (c->column > 0)
      c->column -= 1;
    break;
  case down:
    if (c->column + 1 < b->columns)
      c->column += 1;
    break;
  case left:
    // if we can move left
    if (c->row > 0)
      c->row -= 1;
    break;
  case right:
    // if we can move right
    if (c->row + 1 < b->rows)
      c->row += 1;
    break;
  case invalid:
    // do nothing
    break;
  }
}

static void loop(board *b) {
  cursor *c = malloc(sizeof(cursor));
  c->column = 0;
  c->row = 0;

  while (1) {
    // draw
    draw_board(b, c);
    // input
    move_cursor_in_direction(b, c, read_cursor_movement());
    reset_screen_for_board(b);
  }

  free(c);
}

int main(void) {
  board *b = make_board(3, 3);

  /* for (unsigned long size = 0; size < b->tile_size; size += 1) */
  /*   b->tiles[size] = rand() % 3; */
  loop(b);

  free_board(b);
  return EXIT_SUCCESS;
}
