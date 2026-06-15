// GetCommutateurInfo_RS232.cpp :
// 1) Detecte les ports COM disponibles (en essayant COM1..COM32)
// 2) Menu de selection du port
// 3) Menu de selection du debit
// 4) Ouverture du port, envoi d’une commande au commutateur
// 5) Lecture et affichage de la configuration active (reponse brute)
// 6) Auto-detection du prompt Cisco
// 7) Historique ↑ ↓
// 8) Defilement automatique
// 9) Lecture/ecriture RS232

#include <windows.h>
#include <stdio.h>
#include <conio.h>

#define MAX_COM_PORTS 32
#define RX_BUFFER_SIZE 4096
#define MAX_HISTORY 50
#define C_RESET   "\x1b[0m"
#define C_RED     "\x1b[31m"
#define C_GREEN   "\x1b[32m"
#define C_YELLOW  "\x1b[33m"
#define C_BLUE    "\x1b[34m"
#define C_MAGENTA "\x1b[35m"
#define C_CYAN    "\x1b[36m"
#define C_WHITE   "\x1b[37m"

char history[MAX_HISTORY][256];
int historyCount = 0;
int historyPos = -1;

void EnableVT100();
void ScrollConsole();
void AddHistory(const char* cmd);
void HandleHistory(char* current);
void PrintColoredLine(const char* line);
int DetectPrompt(const char* line);

typedef struct _COM_PORT_INFO {
    int  index;
    char name[16];
} COM_PORT_INFO;

int  DetectPorts(COM_PORT_INFO* ports, int maxPorts);
void AfficherPorts(COM_PORT_INFO* ports, int count);
int  MenuChoixPort(COM_PORT_INFO* ports, int count);
DWORD MenuChoixBaudrate();
HANDLE OuvrirPortSerie(const char* portName, DWORD baudrate);
BOOL ConfigurerPort(HANDLE hCom, DWORD baudrate);
BOOL EnvoyerCommande(HANDLE hCom, const char* cmd);
void LireEtAfficherReponse(HANDLE hCom);

int main()
{
    EnableVT100();
    printf(C_WHITE "=== Terminal RS232 ===\tv:1.01 \t%c Patrice Waechter-Ebling\n" C_RESET,0xB8);
    COM_PORT_INFO ports[MAX_COM_PORTS];
    int nbPorts = DetectPorts(ports, MAX_COM_PORTS);
    if (nbPorts <= 0){printf(C_RED "Aucun port COM detecte.\n" C_RESET);_getch();return 0;}
    printf(C_YELLOW "Ports COM disponibles :\n" C_RESET);
    AfficherPorts(ports, nbPorts);
    int idx = MenuChoixPort(ports, nbPorts);
    if (idx < 0){printf(C_RED "Selection invalide.\n" C_RESET);return 0;}
    DWORD baudrate = MenuChoixBaudrate();
    if (baudrate == 0){printf(C_RED "Debit invalide.\n" C_RESET);return 0;}
    printf(C_WHITE "\nOuverture du port %s à %lu bauds...\n" C_RESET,ports[idx].name, (unsigned long)baudrate);
    HANDLE hCom = OuvrirPortSerie(ports[idx].name, baudrate);
    if (hCom == INVALID_HANDLE_VALUE){printf(C_RED "Erreur : impossible d’ouvrir %s\n" C_RESET, ports[idx].name);_getch();return 0;}
    printf(C_GREEN "\nConnexion etablie. Terminal actif.\n" C_RESET);
    printf(C_WHITE "Tapez vos commandes. ↑ ↓ pour historique.\n\n" C_RESET);
    LireEtAfficherReponse(hCom);
    CloseHandle(hCom);
    return 0;
}
void EnableVT100()
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    mode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
    SetConsoleMode(hOut, mode);
}
void ScrollConsole()
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    GetConsoleScreenBufferInfo(h, &info);
    SMALL_RECT rect;
    rect.Left = 0;
    rect.Top = 1;
    rect.Right = info.dwSize.X - 1;
    rect.Bottom = info.dwSize.Y - 1;
    COORD dest = { 0, 0 };
    CHAR_INFO fill;
    fill.Char.AsciiChar = ' ';
    fill.Attributes = 7;
    ScrollConsoleScreenBuffer(h, &rect, NULL, dest, &fill);
}
void AddHistory(const char* cmd)
{
    if (historyCount < MAX_HISTORY)strcpy(history[historyCount++], cmd);
    else
    {
        for (int i = 1; i < MAX_HISTORY; i++)strcpy(history[i - 1], history[i]);
        strcpy(history[MAX_HISTORY - 1], cmd);
    }
}
void HandleHistory(char* current)
{
    if (!_kbhit()) return;
    int key = _getch();
    if (key != 224) return;
    key = _getch();
    if (key == 72) // ↑
    {
        if (historyPos < historyCount - 1)historyPos++;
        strcpy(current, history[historyCount - 1 - historyPos]);
        printf("\r> %s", current);
    }
    else if (key == 80) // ↓
    {
        if (historyPos > 0){historyPos--;strcpy(current, history[historyCount - 1 - historyPos]);}
        else{historyPos = -1;current[0] = 0;}
        printf("\r> %s", current);
    }
}
void PrintColoredLine(const char* line)
{
    if (DetectPrompt(line) >= 0){printf(C_YELLOW "%s\n" C_RESET, line);return;}
    if (strstr(line, "%") || strstr(line, "Invalid") || strstr(line, "Error")){printf(C_RED "%s\n" C_RESET, line);return;}
    if (strstr(line, "GigabitEthernet") || strstr(line, "FastEthernet") || strstr(line, "Vlan")){printf(C_CYAN "%s\n" C_RESET, line);return;}
    if (strstr(line, "ip ") || strstr(line, "access-list") ||strstr(line, "permit") || strstr(line, "deny")){printf(C_MAGENTA "%s\n" C_RESET, line);return;}
    printf(C_GREEN "%s\n" C_RESET, line);
}
int DetectPrompt(const char* line)
{
    if (strstr(line, "(config-if)#")) return 3;
    if (strstr(line, "(config)#"))    return 2;
    if (strstr(line, "#"))            return 1;
    if (strstr(line, ">"))            return 0;
    return -1;
}
int DetectPorts(COM_PORT_INFO* ports, int maxPorts)
{
    int count = 0;
    for (int i = 1; i <= MAX_COM_PORTS && count < maxPorts; ++i)
    {
        char portName[16];
        if (i < 10)
            wsprintf(portName, "COM%d", i);
        else
            wsprintf(portName, "\\\\.\\COM%d", i);
        HANDLE h = CreateFileA(portName, GENERIC_READ | GENERIC_WRITE,0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            CloseHandle(h);
            COM_PORT_INFO info;
            info.index = i;
            wsprintf(info.name, "COM%d", i);
            ports[count++] = info;
        }
    }
    return count;
}
void AfficherPorts(COM_PORT_INFO* ports, int count)
{
    for (int i = 0; i < count; ++i)
        printf(C_YELLOW "  [%d] %s\n" C_RESET, i + 1, ports[i].name);
}
int MenuChoixPort(COM_PORT_INFO* ports, int count)
{
    int choix = 0;
    printf(C_WHITE "\nChoisissez un port : " C_RESET);
    scanf("%d", &choix);
    if (choix < 1 || choix > count)return -1;
    return choix - 1;
}
DWORD MenuChoixBaudrate()
{
    int choix = 0;
    printf(C_YELLOW "\nDebits disponibles :\n" C_RESET);
    printf("  [1] 9600\n");
    printf("  [2] 19200\n");
    printf("  [3] 38400\n");
    printf("  [4] 57600\n");
    printf("  [5] 115200\n");
    printf(C_WHITE "Votre choix : " C_RESET);
    scanf("%d", &choix);
    switch (choix)
    {
    case 1: return CBR_9600;
    case 2: return CBR_19200;
    case 3: return CBR_38400;
    case 4: return CBR_57600;
    case 5: return CBR_115200;
    }
    return 0;
}
HANDLE OuvrirPortSerie(const char* portName, DWORD baudrate)
{
    char deviceName[32];
    if (lstrlen(portName) > 4)
        wsprintf(deviceName, "\\\\.\\%s", portName);
    else
        wsprintf(deviceName, "%s", portName);
    HANDLE h = CreateFileA(deviceName, GENERIC_READ | GENERIC_WRITE,0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)return INVALID_HANDLE_VALUE;
    if (!ConfigurerPort(h, baudrate)){CloseHandle(h);return INVALID_HANDLE_VALUE;}
    return h;
}
BOOL ConfigurerPort(HANDLE hCom, DWORD baudrate)
{
    DCB dcb;
    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hCom, &dcb)) return FALSE;
    dcb.BaudRate = baudrate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(hCom, &dcb)) return FALSE;
    COMMTIMEOUTS t;
    ZeroMemory(&t, sizeof(t));
    t.ReadIntervalTimeout = 50;
    t.ReadTotalTimeoutConstant = 100;
    SetCommTimeouts(hCom, &t);
    PurgeComm(hCom, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return TRUE;
}
BOOL EnvoyerCommande(HANDLE hCom, const char* cmd)
{
    DWORD n = 0;
    DWORD len = lstrlen(cmd);
    if (!WriteFile(hCom, cmd, len, &n, NULL)) return FALSE;
    return (n == len);
}
void LireEtAfficherReponse(HANDLE hCom)
{
    BYTE buffer[RX_BUFFER_SIZE];
    char line[1024];
    int pos = 0;
    int lineCount = 0;
    char cmd[256];
    cmd[0] = 0;
    while (1)
    {
        HandleHistory(cmd);
        if (_kbhit())
        {
            int c = _getch();
            if (c == 13) // ENTER
            {
                printf("\n");
                AddHistory(cmd);
                historyPos = -1;
                strcat(cmd, "\r\n");
                EnvoyerCommande(hCom, cmd);
                cmd[0] = 0;
                printf("> ");
            }
            else if (c == 8) // BACKSPACE
            {
                int len = lstrlen(cmd);
                if (len > 0){cmd[len - 1] = 0;printf("\b \b");}
            }
            else if (c >= 32 && c <= 126)
            {
                int len = lstrlen(cmd);
                if (len < 254){cmd[len] = (char)c;cmd[len + 1] = 0;printf("%c", c);}
            }
        }
        DWORD n = 0;
        ReadFile(hCom, buffer, RX_BUFFER_SIZE - 1, &n, NULL);
        if (n > 0)
        {
            for (DWORD i = 0; i < n; i++)
            {
                char c = buffer[i];
                if (c == '\n' || pos >= 1023)
                {
                    line[pos] = 0;
                    PrintColoredLine(line);
                    pos = 0;
                    lineCount++;
                    if (lineCount > 200){ScrollConsole();lineCount = 0;}
                }
                else if (c != '\r'){line[pos++] = c;}
            }
        }
        Sleep(5);
    }
}
