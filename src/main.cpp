#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include <cstring>
#include <vector>


#define WIFI_SSID "REPLACE_WITH_YOUR_SSID"
#define WIFI_PASSWORD "REPLACE_WITH_YOUR_PASSWORD"

#define SERVER_ADDRESS "REPLACE_WITH_SERVER_ADDRESS"

enum DeviceState
{
    DEVICE_STATE_NONE = 0,
    DEVICE_STATE_INITIALIZING,
    DEVICE_STATE_ERROR,
    DEVICE_STATE_WIFI_CONNECTING,
    DEVICE_STATE_WIFI_SCANNING,
    DEVICE_STATE_WIFI_CONNECTED,
    DEVICE_STATE_IDLE,
    DEVICE_STATE_SERVER_CONNECTING,
    DEVICE_STATE_SERVER_CONNECTED
};

class BinaryReader
{
private:
    const unsigned char* m_pucData;
    unsigned int m_uiOffset;

public:
    BinaryReader(const unsigned char* pucData) : m_pucData(pucData), m_uiOffset(0) {}

    unsigned char ReadByte()
    {
        return m_pucData[m_uiOffset++];
    }

    void SkipByte()
    {
        m_uiOffset++;
    }

    short ReadShort()
    {
        short sValue = 0;
        unsigned char* pucBytes = (unsigned char*)&sValue;
        pucBytes[0] = m_pucData[m_uiOffset++];
        pucBytes[1] = m_pucData[m_uiOffset++];
        return sValue;
    }

    void SkipShort()
    {
        m_uiOffset += 2;
    }

    long ReadLong()
    {
        long lValue = 0;
        unsigned char* pucBytes = (unsigned char*)&lValue;
        pucBytes[0] = m_pucData[m_uiOffset++];
        pucBytes[1] = m_pucData[m_uiOffset++];
        pucBytes[2] = m_pucData[m_uiOffset++];
        pucBytes[3] = m_pucData[m_uiOffset++];
        return lValue;
    }

    void SkipLong()
    {
        m_uiOffset += 4;
    }

    String ReadString()
    {
        String szResult;

        while(true)
        {
            unsigned char cChar = m_pucData[m_uiOffset++];
            if(cChar == 0)
                break;

            szResult += (char)cChar;
        }

        return szResult;
    }

    void SkipString()
    {
        while(m_pucData[m_uiOffset++] != 0);
    }
};

class BinaryWriter
{
private:
    std::vector<unsigned char> m_vecBuffer;

public:
    void WriteByte(unsigned char ucValue)
    {
        m_vecBuffer.push_back(ucValue);
    }

    void WriteShort(short sValue)
    {
        unsigned char* pucBytes = (unsigned char*)&sValue;
        m_vecBuffer.push_back(pucBytes[0]);
        m_vecBuffer.push_back(pucBytes[1]);
    }

    void WriteLong(long lValue)
    {
        unsigned char* pucBytes = (unsigned char*)&lValue;
        m_vecBuffer.push_back(pucBytes[0]);
        m_vecBuffer.push_back(pucBytes[1]);
        m_vecBuffer.push_back(pucBytes[2]);
        m_vecBuffer.push_back(pucBytes[3]);
    }

    void WriteString(const String& szValue)
    {
        for(unsigned int i = 0; i < szValue.length(); i++)
        {
            m_vecBuffer.push_back(szValue[i]);
        }

        m_vecBuffer.push_back(0);
    }

    unsigned char* GetBuffer() const { return (unsigned char*)m_vecBuffer.data(); }

    size_t GetSize() const { return m_vecBuffer.size(); }
};

void SetDeviceState();
void SetDeviceState(DeviceState eDeviceState, bool bHandleState = true);
void HandleDeviceState();
void SendA2SInfoPacket(long lChallenge = 0xFFFFFFFF);
void HandleS2CPacket(BinaryReader* pPacket);


LiquidCrystal_I2C* g_pLCDDisplay = NULL;
AsyncUDP* g_pUDPClient = NULL;
DeviceState g_eDeviceState = DEVICE_STATE_NONE;
DeviceState g_eNextDeviceState = DEVICE_STATE_NONE;
char g_aszErrorMessage[2][16 + 1];
float g_flNextUpdateTime = 0.0f;
char g_szSSID[32 + 1];


void setup()
{
    SetDeviceState(DEVICE_STATE_INITIALIZING);
}

void cleanup() {
    if(g_pLCDDisplay != NULL) {
        delete g_pLCDDisplay;
        g_pLCDDisplay = NULL;
    }

    if(g_pUDPClient != NULL) {
        delete g_pUDPClient;
        g_pUDPClient = NULL;
    }
}

void loop()
{
    if(g_flNextUpdateTime && millis() >= g_flNextUpdateTime)
    {
        g_flNextUpdateTime = 0.0f;

        switch(g_eDeviceState)
        {
            case DEVICE_STATE_ERROR:
            {
                if(g_eNextDeviceState != DEVICE_STATE_NONE)
                {
                    SetDeviceState(g_eNextDeviceState);
                    g_eNextDeviceState = DEVICE_STATE_NONE;
                }

                break;
            }

            case DEVICE_STATE_IDLE:
            {
                SetDeviceState(DEVICE_STATE_IDLE);

                break;
            }
        }
    }
}


void OnWiFiEvent(WiFiEvent_t eEvent, WiFiEventInfo_t sInfo)
{
    switch(eEvent)
    {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        {
            SetDeviceState(DEVICE_STATE_WIFI_CONNECTED);
            break;
        }

        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        {
            strcpy(g_aszErrorMessage[0], "No WiFi");
            strcpy(g_aszErrorMessage[1], "Reconnecting...");

            g_flNextUpdateTime = millis() + 5000;
            g_eNextDeviceState = DEVICE_STATE_WIFI_CONNECTING;
            SetDeviceState(DEVICE_STATE_ERROR);
            break;
        }
    }
}

void OnUDPPacketReceived(AsyncUDPPacket& sPacket)
{
    if(sPacket.length() > 0)
    {
        BinaryReader* pReader = new BinaryReader(sPacket.data());

        long lHeader = pReader->ReadLong();
        if(lHeader == 0xFFFFFFFF)
        {
            HandleS2CPacket(pReader);
        }

        delete pReader;
    }
}


void SetDeviceState()
{
    HandleDeviceState();
}

void SetDeviceState(DeviceState eDeviceState, bool bHandleState)
{
    g_eDeviceState = eDeviceState;

    if(bHandleState)
    {
        HandleDeviceState();
    }
}

void HandleDeviceState()
{
    switch(g_eDeviceState)
    {
        case DEVICE_STATE_INITIALIZING:
        {
            if(g_pLCDDisplay != NULL) {
                delete g_pLCDDisplay;
                g_pLCDDisplay = NULL;
            }

            if(g_pUDPClient != NULL) {
                delete g_pUDPClient;
                g_pUDPClient = NULL;
            }

            g_pLCDDisplay = new LiquidCrystal_I2C(0x27, 16, 2);
            g_pUDPClient = new AsyncUDP(); g_pUDPClient->onPacket(OnUDPPacketReceived);

            g_pLCDDisplay->init();
            g_pLCDDisplay->backlight();

            g_pLCDDisplay->clear();
            g_pLCDDisplay->setCursor(0, 0); g_pLCDDisplay->print("Initializing...");
            SetDeviceState(DEVICE_STATE_WIFI_CONNECTING);

            break;
        }

        case DEVICE_STATE_ERROR:
        {
            g_pLCDDisplay->clear();
            g_pLCDDisplay->setCursor(0, 0); g_pLCDDisplay->print(g_aszErrorMessage[0]);
            g_pLCDDisplay->setCursor(0, 1); g_pLCDDisplay->print(g_aszErrorMessage[1]);

            break;
        }

        case DEVICE_STATE_WIFI_CONNECTING:
        {
            if(strlen(g_szSSID) == 0)
            {
                strcpy(g_szSSID, WIFI_SSID);
            }

            if(strlen(g_szSSID) == 0)
            {
                SetDeviceState(DEVICE_STATE_WIFI_SCANNING);
                break;
            }

            g_pLCDDisplay->clear();
            g_pLCDDisplay->setCursor(0, 0); g_pLCDDisplay->print("Connecting to:");
            g_pLCDDisplay->setCursor(0, 1); g_pLCDDisplay->print(g_szSSID);

            WiFi.begin((const char*)g_szSSID, WIFI_PASSWORD);
            WiFi.onEvent(OnWiFiEvent);

            break;
        }

        case DEVICE_STATE_WIFI_SCANNING:
        {
            g_pLCDDisplay->clear();
            g_pLCDDisplay->setCursor(0, 0); g_pLCDDisplay->print("SSID is not set");
            g_pLCDDisplay->setCursor(0, 1); g_pLCDDisplay->print("Scanning...");

            int iNetworks = WiFi.scanNetworks();
            bool bFoundNetwork = false;

            if(iNetworks > 0)
            {
                for(int iNetworkIndex = 0; iNetworkIndex < iNetworks; ++iNetworkIndex)
                {
                    if(WiFi.encryptionType(iNetworkIndex) == WIFI_AUTH_OPEN)
                    {
                        String szNetworkSSID = WiFi.SSID(iNetworkIndex);

                        if(!szNetworkSSID.isEmpty())
                        {
                            strcpy(g_szSSID, szNetworkSSID.c_str());
                            bFoundNetwork = true;
                            break;
                        }
                    }
                }
            }

            if(!bFoundNetwork)
            {
                strcpy(g_aszErrorMessage[0], "No network found");
                strcpy(g_aszErrorMessage[1], "Retrying...");

                g_flNextUpdateTime = millis() + 5000;
                g_eNextDeviceState = DEVICE_STATE_WIFI_CONNECTING;
                SetDeviceState(DEVICE_STATE_ERROR);
                break;
            }

            g_pLCDDisplay->clear();
            g_pLCDDisplay->setCursor(0, 0); g_pLCDDisplay->print("SSID found:");
            g_pLCDDisplay->setCursor(0, 1); g_pLCDDisplay->print(g_szSSID);
            SetDeviceState(DEVICE_STATE_WIFI_CONNECTING);
            break;
        }

        case DEVICE_STATE_WIFI_CONNECTED:
        {
            g_pLCDDisplay->clear();
            g_pLCDDisplay->setCursor(0, 0); g_pLCDDisplay->print("Connected to:");
            g_pLCDDisplay->setCursor(0, 1); g_pLCDDisplay->print(g_szSSID);

            SetDeviceState(DEVICE_STATE_IDLE);

            break;
        }

        case DEVICE_STATE_IDLE:
        {
            SetDeviceState(DEVICE_STATE_SERVER_CONNECTING);

            break;
        }

        case DEVICE_STATE_SERVER_CONNECTING:
        {
            String szServerAddress;
            int iServerPort;

            int iColonPos = String(SERVER_ADDRESS).indexOf(':');
            if(iColonPos != -1) {
                szServerAddress = String(SERVER_ADDRESS).substring(0, iColonPos);
                iServerPort = String(SERVER_ADDRESS).substring(iColonPos + 1).toInt();
            }
            else
            {
                szServerAddress = SERVER_ADDRESS;
                iServerPort = 27015;
            }

            IPAddress ipServerIP;
            if(WiFi.hostByName(szServerAddress.c_str(), ipServerIP) == 1) {
                if(!g_pUDPClient->connect(ipServerIP, iServerPort))
                {
                    strcpy(g_aszErrorMessage[0], "Connect failed");
                    strcpy(g_aszErrorMessage[1], "Retrying...");

                    g_flNextUpdateTime = millis() + 5000;
                    g_eNextDeviceState = DEVICE_STATE_SERVER_CONNECTING;
                    SetDeviceState(DEVICE_STATE_ERROR);
                    return;
                }
            }
            else
            {
                strcpy(g_aszErrorMessage[0], "DNS failed");
                strcpy(g_aszErrorMessage[1], "Retrying...");

                g_flNextUpdateTime = millis() + 5000;
                g_eNextDeviceState = DEVICE_STATE_SERVER_CONNECTING;
                SetDeviceState(DEVICE_STATE_ERROR);
                return;
            }

            SetDeviceState(DEVICE_STATE_SERVER_CONNECTED);

            break;
        }

        case DEVICE_STATE_SERVER_CONNECTED:
        {
            SendA2SInfoPacket();

            break;
        }
    }
}

void SendA2SInfoPacket(long lChallenge)
{
    BinaryWriter* pWriter = new BinaryWriter();

    pWriter->WriteLong(0xFFFFFFFF);
    pWriter->WriteByte(0x54);
    pWriter->WriteString("Source Engine Query");
    pWriter->WriteLong(lChallenge);

    g_pUDPClient->write(pWriter->GetBuffer(), pWriter->GetSize());
    delete pWriter;
}

void HandleS2CPacket(BinaryReader* pPacket)
{
    unsigned char ucHeader = pPacket->ReadByte();
    switch(ucHeader)
    {
        case 0x41:
        {
            long lChallenge = pPacket->ReadLong();

            SendA2SInfoPacket(lChallenge);

            break;
        }

        case 0x49:
        {
            pPacket->SkipByte();
            pPacket->SkipString();
            String szMap = pPacket->ReadString();
            pPacket->SkipString();
            pPacket->SkipString();
            pPacket->SkipShort();
            unsigned char ucPlayers = pPacket->ReadByte();
            unsigned char ucMaxPlayers = pPacket->ReadByte();
            unsigned char ucBots = pPacket->ReadByte();

            char szBottomText[16 + 1];

            if(ucBots > 0)
            {
                int iRealPlayers = ucPlayers - ucBots;
                snprintf(szBottomText, sizeof(szBottomText) - 1, "%d/%d (%d)", iRealPlayers, ucMaxPlayers, ucBots);
            }
            else
            {
                snprintf(szBottomText, sizeof(szBottomText) - 1, "%d/%d players", ucPlayers, ucMaxPlayers);
            }

            g_pLCDDisplay->clear();
            g_pLCDDisplay->setCursor(0, 0); g_pLCDDisplay->print(szMap);
            g_pLCDDisplay->setCursor(0, 1); g_pLCDDisplay->print(szBottomText);

            g_flNextUpdateTime = millis() + 30000;
            SetDeviceState(DEVICE_STATE_IDLE, false);

            break;
        }
    }
}
