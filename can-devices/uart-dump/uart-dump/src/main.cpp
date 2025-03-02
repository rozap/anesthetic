#include <SPI.h>
#include <mcp2515.h>

struct can_frame canMsg;
MCP2515 mcp2515(PA4);

void setup()
{
  Serial1.begin(9600);

  SPI.setMOSI(PA7);
  SPI.setMISO(PA6);
  SPI.setSCLK(PA5);
  SPI.begin();

  mcp2515.reset();
  mcp2515.setBitrate(CAN_100KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();
  // mcp2515.setClkOut

  Serial1.println("------- CAN Read ----------");
  Serial1.println("ID  DLC   DATA");
}

void loop()
{
  MCP2515::ERROR res = mcp2515.readMessage(&canMsg);
  if (res == MCP2515::ERROR_OK)
  {
    Serial1.print(canMsg.can_id, HEX); // print ID
    Serial1.print(" ");
    Serial1.print(canMsg.can_dlc, HEX); // print DLC
    Serial1.print(" ");

    for (int i = 0; i < canMsg.can_dlc; i++)
    { // print the data
      Serial1.print(canMsg.data[i], HEX);
      Serial1.print(" ");
    }

    Serial1.println();
  }
  else
  {
    if (millis() % 1000 == 0)
    {
      Serial1.print("No message: ");
      switch (res)
      {
      case MCP2515::ERROR_OK:
        Serial1.print(" - Err Ok?");
        break;

      case MCP2515::ERROR_FAIL:
        Serial1.print(" - Err Fail");
        break;
      case MCP2515::ERROR_ALLTXBUSY:
        Serial1.print(" - Err all tx busy");
        break;
      case MCP2515::ERROR_FAILINIT:
        Serial1.print(" - Err fail init");
        break;
      case MCP2515::ERROR_FAILTX:
        Serial1.print(" - Err fail tx");
        break;
      case MCP2515::ERROR_NOMSG:
        Serial1.print(" - Err No Message");
        break;
      }
      Serial1.print(res);
      Serial1.println();
    }
  }
}