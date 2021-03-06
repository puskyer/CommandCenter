#include "CellDriver.h"
#include <string.h>
#include <iostream>
#include <algorithm>
#include <stdio.h>

uint64_t cellMillis()
{
    timeval currentTime;
    gettimeofday(&currentTime, NULL);
    return currentTime.tv_sec * 1000 + currentTime.tv_usec / 1000;
}

CellDriver::CellDriver(Uart* uart)
{
    this->uart = uart;
    shouldEchoUartToStdout = false;

    //think about size of buffer - may cut off message if more than 1 is sent
    responseBuffer.reserve(200);
    isWaitingForOk = false;
    lastRetrieveMessagesTime = cellMillis();
    lastCellTowerTime = cellMillis();
    shouldEchoUartToStdout = true;
    towerInfoList = "";
    // A command to set the texting mode
    setupCellModule();
}

void CellDriver::setupCellModule()
{
    // Command to set to texting mode
#ifdef CELLPRINTDEBUG
    printf("Setting to text mode\n\r");
#endif
    commandQueue.push_back("AT+CMGF=1\r");
}

//the update function switches between checking if there is something in the buffer from a last
//send AT command and sending out the next AT command.
//0 is incomplete parse
int8_t CellDriver::update()
{
    // If we have been waiting for 10 seconds, time it out...
    if (isWaitingForOk && (cellMillis() > lastWaitingForOKTime + 10000))
    {
        isWaitingForOk = false;
        isWaitingForPrompt = false;
        std::cout << "CELL ERROR: Timed out waiting for an OK/prompt" << std::endl;
        // Try setting text mode again?
        setupCellModule();
    }
    
    // Check for messages every 10 seconds..
    if (cellMillis() > lastRetrieveMessagesTime + 10000)
    {
#ifdef CELLPRINTDEBUG
        std::cout << "CELL: Calling retrieve text messages" << std::endl;
#endif
        retrieveTextMessages();
        lastRetrieveMessagesTime = cellMillis();
    }

    if (cellMillis() + 2000 > lastCellTowerTime + 10000)
    {
#ifdef CELLPRINTDEBUG
        std::cout << "CELL: Asking for tower information" << std::endl;
#endif
        queuePositionFix();
        lastCellTowerTime = cellMillis();        
    }

    for(int32_t byte = uart->readByte(); byte != -1; byte = uart->readByte())
    {
        if (shouldEchoUartToStdout)
        {
            std::cout << (char)byte;
        }

        if (isWaitingForPrompt && (byte == '>'))
        {
#ifdef CELLPRINTDEBUG
            printf("Trying to output the second half of the send text\n\r");
#endif
            (*uart) << commandQueue.front();
            commandQueue.pop_front();
            isWaitingForPrompt = false;   
        }

        if ((byte == '\r' || byte == '\n') && responseBuffer.length() > 0)
        {
            //Parses it line by line looking for something useful to do with string
            int8_t parseResponse = parse(responseBuffer); //does this syntax work for a string?
            //cleanse the string
            responseBuffer = "";
            return parseResponse;
        }
        else if (byte != '\n' && byte != '\r')
        {
            // add byte to string if not on an end of line character.
            responseBuffer += byte;
        }
    }
    
    //We can send a new command if we are not waiting for the  to finish an old one
    if(!this->isWaitingForOk)
    {
#ifdef CELLPRINTDEBUG
//            printf("Thinks it can send a command\n\r");
#endif        
        //Send out the oldest element in the commandQueue, if any
        if (commandQueue.size() > 0)
        {
#ifdef CELLPRINTDEBUG
            printf("Thinks it has a command to send\n\r");
#endif
            lastWaitingForOKTime = cellMillis();
            isWaitingForOk = true;
            
            (*uart) << commandQueue.front();
            
            // If we are beginning to send a text message, we need to wait for a prompt,
            // after this initial request
            if (0 == strcmp(commandQueue.front().substr(0,7).c_str(),"AT+CMGS"))
            {
#ifdef CELLPRINTDEBUG
            printf("Thinks it is sending the first half of a text\n\r");
#endif   
                isWaitingForPrompt = true;
            } 
            std::cout << commandQueue.front() << std::endl;
            //After command is sent then removes it from the Queue
            commandQueue.pop_front();
        }
    }
/*
        if (commandQueue.size() > 0)
        {
        std::cout << "First in queue: " << std::endl;        
        std::cout << commandQueue.front() << std::endl;
        }
*/    
    return false;
}


//This part is responsible for adding sms message to the list of commands.
void CellDriver::queueTextMessage(const char* recipientPhoneNumber, const char* textMessage)
{
    //Needs to be split into two messages
    //Sent before prompt
    std::stringstream firstHalf;
    firstHalf << "AT+CMGS=\"" << recipientPhoneNumber << "\"\r";
    commandQueue.push_back(firstHalf.str());
    
    //Sent after prompt
    std::stringstream secondHalf;
    secondHalf << textMessage << '\x1A';
    commandQueue.push_back(secondHalf.str());
    
    std::cout << "Queued the Text Message" << std::endl;
}

// ATV1 --verbose responses


//Sends command to retrieve all messages from cell shield; returns "ok" if there 
//are no messages >> wrong - returns 0 if no messages!
//as the messages maybe broken up to multiple ones.
void CellDriver::retrieveTextMessages()
{ 
    commandQueue.push_back("AT+CMGL=\"ALL\"\r");
}  


void CellDriver::deleteMessage(TextMessage textMessage)
{
    std::stringstream deleteCommand;
    deleteCommand << "AT+CMGD=" << textMessage.index << "\r";

    commandQueue.push_front(deleteCommand.str());

    if (shouldEchoUartToStdout)
    {
#ifdef CELLPRINTDEBUG
        printf("Trying to output the queue\n\r");
#endif
        std::cout << commandQueue.front() << std::endl;
    }
}

TextMessage CellDriver::getTextMessage()
{
    if (messageQueue.size() > 0)
    {
        TextMessage text = messageQueue.front();
        messageQueue.pop();
        return text;
    }
    TextMessage text("", "", "", "", "", "");
    return text;
}

//Sends the command to get information about the nearby cell
//towers that it is close to the cell shield
//Response format
//+CNCI: Index of Cell, BCCH, BSIC, LAC, Rxlev, Cell ID, MCC, MNC 
void CellDriver::queuePositionFix()
{
    //Add get cell tower info command to end of queue
    commandQueue.push_back("AT+CNCI=A\r");
}

//Response example for +CNCI
/*
+CNCI: 6
+CNCI: 0,240,10,1395,49,d7d4,310,0
+CNCI: 1,251,50,1395,31,d7d5,310,0
+CNCI: 2,33372,20,1395,49,d7d6,310,0
+CNCI: 3,3./3365,21,1395,22,c76b,310,0
+CNCI: 4,247,59,1395,22,4f76,310,0
+CNCI: 5,33364,3,1395,25,d7d8,310,0

OK
*/


// Handles a single line of input from the cell shield
int8_t CellDriver::parse(std::string inputResponse)
{
#ifdef CELLPRINTDEBUG
            printf("This is what it is trying to parse:\n\r");
            std::cout << inputResponse << std::endl;
#endif  
    if (isReceivingTextMessage)
    {
        messageData = inputResponse;
        TextMessage newMessage;
        newMessage.index = index;
        newMessage.messageType = messageType;
        newMessage.number = number;
        newMessage.name = name;
        newMessage.time = time;
        newMessage.messageData = messageData;
#ifdef CELLPRINTDEBUG
        printf("About to push messgeQueue\n\r");
#endif
        messageQueue.push(newMessage);
#ifdef CELLPRINTDEBUG
        printf("Succeeded to push newMessage to messageQueue\n\r");
#endif
        isReceivingTextMessage = false;
        return true;
    }

    if (strcmp(inputResponse.substr(0,2).c_str(), "OK") == 0)
    {
        this->isWaitingForOk = false;
        return false;
    }

    // Response to a request to list all text messages
    // This details exactly one text message (there may be multiple of these responses)
    if (strcmp(inputResponse.substr(0,5).c_str(), "+CMGL") == 0)
    {
        // There is a colon immediately following the message the +CMGL, so we skip over it to the comma separated data
        std::stringstream textInfo(inputResponse.substr(6));
        getline(textInfo, index, ',');
        getline(textInfo, messageType, ',');
        getline(textInfo, number, ',');
        getline(textInfo, name, ',');
        getline(textInfo, time, ',');

        messageData = "";
#ifdef CELLPRINTDEBUG
        printf("Loaded text information into the text class\n\r");
#endif
        isReceivingTextMessage = true;
        return false;
    }

    // Response to a request for cell tower information
    if (strcmp(inputResponse.substr(0,5).c_str(), "+CNCI") == 0)
    {
        if(!isReceivingCellTowers)
        {
            isReceivingCellTowers = true;
            
            // The first +CNCI response gives us the number of towers we will get info on
            // First clear out old info
            towerInfoList.erase();
            
            // Returning 0 towers to receive on error is perfectly acceptable here...
            // And we skip the first 6 characters (+CNCI:)
            totalTowersToReceive = strtol(inputResponse.substr(6).c_str(), NULL, 10);
#ifdef CELLPRINTDEBUG
            printf("Towers to receive: %i\n\r", totalTowersToReceive);
#endif
        } 
        else
        {
            towerInfoList.append(inputResponse);
            towerInfoList.append(" "); //put a space between entries
#ifdef CELLPRINTDEBUG
            printf("Towers Read: %i\n\r",strtol(inputResponse.substr(6).c_str(), NULL, 10));
#endif
            if (strtol(inputResponse.substr(6).c_str(), NULL, 10) >= totalTowersToReceive - 1)
            {
                
                // We have received them all!
                newTowerInfo = true;
                isReceivingCellTowers = false;
            }
        }
        return false;
    }
    
    return false;
}

TextMessage::TextMessage(){}

std::string CellDriver::getLastTowerInformation()
{
    return towerInfoList;
}
