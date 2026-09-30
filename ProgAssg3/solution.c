// Module Assignment: Colossus Airlines Reservation System & AI Stress Testing Overview

// // My role: 
//     - you will write a C-based seating reservation system for Colossus Airlines and use an AI tool to generate automated test harnesses to stress-test your system's input stream handling.
//     - You will build a menu-driven C program that manages seat assignments using arrays of structures.

#include <stdio.h>
#include <string.h>

#define LINE_SIZE 50


struct seat {
    int seatID;
    int assigned;
    char lastname[50];
    char firstname[50];
};


// returns -1 on end of file, 1 if the line was too long, 0 if ok
int read_line(char *buf, int size)
{
    int len;
    int c;

    if (fgets(buf, size, stdin) == NULL)
        return -1;

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
    {
        buf[len - 1] = '\0';
        return 0;
    }
        c = getchar();
    if (c == EOF)
        return 0;
    while (c != '\n' && c != EOF)
        c = getchar();
    return 1;
}


// returns the menu letter, 0 if invalid, EOF if input ended
int get_choice(void)
{
    char line[LINE_SIZE];
    int status = read_line(line, LINE_SIZE);

    if (status == -1)
        return EOF;
    if (status == 1 || strlen(line) != 1)
        return 0;
    return line[0];
}


// A blank line at any prompt cancels
void assign_seat(struct seat flight[])
{
    char line[LINE_SIZE];
    int num;

    printf("Seat number (blank to cancel): ");
    if (read_line(line, LINE_SIZE) == -1)
        return;
    if (line[0] == '\0')
        return;
    if (sscanf(line, "%d", &num) != 1 || num < 1 || num > 24)
    {
        printf("Invalid seat number.\n");
        return;
    }
    if (flight[num - 1].assigned)
    {
        printf("Seat %d is already assigned.\n", num);
        return;
    }

    printf("Last name (blank to cancel): ");
    read_line(flight[num - 1].lastname, 50);
    if (flight[num - 1].lastname[0] == '\0')
        return;

    printf("First name (blank to cancel): ");
    read_line(flight[num - 1].firstname, 50);
    if (flight[num - 1].firstname[0] == '\0')
    {
        flight[num - 1].lastname[0] = '\0';
        return;
    }

    flight[num - 1].assigned = 1;
    printf("Seat %d assigned.\n", num);
}


void count_empty(struct seat flight[])
{
    int i;
    int count = 0;

    for (i = 0; i < 24; i++)
        if (!flight[i].assigned)
            count++;
    printf("Empty seats: %d\n", count);
}


void list_empty(struct seat flight[])
{
    int i;

    printf("Empty seats:");
    for (i = 0; i < 24; i++)
        if (!flight[i].assigned)
            printf(" %d", flight[i].seatID);
    printf("\n");
}


void list_alphabetical(struct seat flight[])
{
    int order[24];
    int n = 0;
    int i, j, temp;

    for (i = 0; i < 24; i++)
        if (flight[i].assigned)
            order[n++] = i;

    if (n == 0)
    {
        printf("No seats assigned.\n");
        return;
    }

    // bubble sort the assigned seats by last name
    for (i = 0; i < n - 1; i++)
        for (j = 0; j < n - 1 - i; j++)
            if (strcmp(flight[order[j]].lastname, flight[order[j + 1]].lastname) > 0)
            {
                temp = order[j];
                order[j] = order[j + 1];
                order[j + 1] = temp;
            }

    for (i = 0; i < n; i++)
        printf("Seat %d: %s, %s\n", flight[order[i]].seatID,
               flight[order[i]].lastname, flight[order[i]].firstname);
}


void delete_seat(struct seat flight[])
{
    char line[LINE_SIZE];
    int num;

    printf("Seat number to delete (blank to cancel): ");
    if (read_line(line, LINE_SIZE) == -1)
        return;
    if (line[0] == '\0')
        return;
    if (sscanf(line, "%d", &num) != 1 || num < 1 || num > 24)
    {
        printf("Invalid seat number.\n");
        return;
    }
    if (!flight[num - 1].assigned)
    {
        printf("Seat %d is not assigned.\n", num);
        return;
    }

    flight[num - 1].assigned = 0;
    flight[num - 1].lastname[0] = '\0';
    flight[num - 1].firstname[0] = '\0';
    printf("Seat %d assignment deleted.\n", num);
}



// Second-level menu

void flight_menu(struct seat flight[])
{
    int choice;

    do {
        printf("\na) Show number of empty seats\n");
        printf("b) Show list of empty seats\n");
        printf("c) Show alphabetical list of seats\n");
        printf("d) Assign a customer to a seat\n");
        printf("e) Delete a seat assignment\n");
        printf("f) Return to Main Menu\n");
        printf("Choice: ");
        choice = get_choice();

        switch (choice) {
        case 'a': count_empty(flight); break;
        case 'b': list_empty(flight); break;
        case 'c': list_alphabetical(flight); break;
        case 'd': assign_seat(flight); break;
        case 'e': delete_seat(flight); break;
        case 'f': break;
        default:  printf("Invalid choice.\n");
        }
    } while (choice != 'f' && choice != EOF);
}


// Two arrays of struct seat for both inbound and outbound
// Write a for loop to initialize every seat for both inbound and outbound seats instead of writing it manually

int main(void)
{
    struct seat outbound[24];
    struct seat inbound[24];
    int choice;
    int i;


    // initializes every inbound and outbound seats
    for (i = 0; i < 24; i++) {
        outbound[i].seatID = i + 1;
        outbound[i].assigned = 0;
        outbound[i].lastname[0] = '\0';
        outbound[i].firstname[0] = '\0';

        inbound[i].seatID = i + 1;
        inbound[i].assigned = 0;
        inbound[i].lastname[0] = '\0';
        inbound[i].firstname[0] = '\0';
    }


    do {
        printf("\na) Outbound Flight\n");
        printf("b) Inbound Flight\n");
        printf("c) Quit\n");
        printf("Choice: ");
        choice = get_choice();

        switch (choice) {
        case 'a': flight_menu(outbound); break;
        case 'b': flight_menu(inbound);  break;
        case 'c': printf("Goodbye.\n");  break;
        default:  printf("Invalid choice.\n");
        }
    } while (choice != 'c' && choice != EOF);

    return 0;
}