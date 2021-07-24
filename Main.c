#include "Declarations.h"

void InitProgram(int argc, char *argv[]);
void ColorRaw(CHAR_INFO *mainchInfo, int row, int col, COORD cd, int sizex, int sizey);
void DrawRaw(int row, int col, int len, unsigned char color, char *text, COORD cd);

int main(int argc, char *argv[])
{
	char string[] = "Demo stirng";
	char *temp;
	
	unsigned char colour;
	int row, col;

	
	InitProgram(argc, argv);

	// single chcaracter on row 5 , col 10
    for (row = 6; row <= 18; row++)
	{
	   for (col = 20; col <= 60;col++)
	   {

		
	
	mainchInfo[row * coord.X + col].Char.AsciiChar = ' ';
	mainchInfo[row * coord.X + col].Attributes = BACKGROUND_BLUE;
       }
    }
	put_screen(&coord, mainchInfo);

	

	// string on row 6, start col 5

	for( row = 6, col = 5, temp = string; *temp != 0 ; col++, temp++){
		mainchInfo[row * coord.X + col].Char.AsciiChar = *temp;
		mainchInfo[row * coord.X + col].Attributes = FOREGROUND_GREEN | FOREGROUND_INTENSITY;

	}
	put_screen(&coord, mainchInfo);

	// string on row 6, start col 5, colours changed

	colour = 1;
	for( row = 7, col = 5, temp = string; *temp != 0 ; col++, temp++){
		mainchInfo[row * coord.X + col].Char.AsciiChar = *temp;
		mainchInfo[row * coord.X + col].Attributes = colour++;

	}
	put_screen(&coord, mainchInfo);

	GetKey();
	clrScr();
	return 1;
}

void ColorRaw(CHAR_INFO *mainchInfo, int row, int col, COORD cd, int sizex, int sizey)
{

	int x, y;
	for (y = col; y <= sizey; y++)
	{
		for (x = row; x <= sizex; x++)
		{
			mainchInfo[y * cd.X + x].Char.AsciiChar = ' ';
			mainchInfo[y * cd.X + x].Attributes = BACKGROUND_BLUE;
		}
	}
	put_screen(&cd, x);
	
}

void DrawRaw(int row, int col, int len, unsigned char color, char *text,COORD cd)
{
	int pos;

	for (pos = 0; len != 0; pos++, text++, len--)
	{
		mainchInfo[row * cd.X + col + pos].Char.AsciiChar = *text;
		mainchInfo[row * cd.X + col + pos].Attributes = color;
	}
	put_screen(&cd, pos);
}



int GetKey()
{
	int ch;
	ch = _getch();
	if (ch == 0 || ch == 224){
		ch = _getch();
		ch += 256;
	}
	return ch;
}

void InitProgram(int argc, char *argv[])
{

	sprintf(Caption,"Project PIK II, DEMO"); 


	mainchInfo = get_screen(&coord);

	init_console(KBValue);
}