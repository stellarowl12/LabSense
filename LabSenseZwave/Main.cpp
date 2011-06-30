//-----------------------------------------------------------------------------
//
//	Main.cpp
//
//	Minimal application to test OpenZWave.
//
//	Creates an OpenZWave::Driver and the waits.  In Debug builds
//	you should see verbose logging to the console, which will
//	indicate that communications with the Z-Wave network are working.
//
//	Copyright (c) 2010 Mal Lansell <mal@openzwave.com>
//
//
//	SOFTWARE NOTICE AND LICENSE
//
//	This file is part of OpenZWave.
//
//	OpenZWave is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Lesser General Public License as published
//	by the Free Software Foundation, either version 3 of the License,
//	or (at your option) any later version.
//
//	OpenZWave is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Lesser General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with OpenZWave.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Group.h"
#include "Notification.h"
#include "ValueStore.h"
#include "Value.h"
#include "ValueBool.h"
#include "Log.h"

// Includes for Zeromq transport layer to make zwave notifications decoupled from updating SensorSafe over http request
#include <zmq.hpp>


// Defines
// For Aeon Labs Door/Window Sensor, the following command classes send events
#define COMMAND_CLASS_BASIC 0x20
#define COMMAND_CLASS_SENSOR_BINARY 0x30
#define COMMAND_CLASS_WAKE_UP 0x84
#define COMMAND_CLASS_BATTERY 0x80
#define COMMAND_CLASS_ALARM 0x71
#define COMMAND_CLASS_VERSION 0x86

// For HSM-100, we have COMMAND_CLASS_BASIC, COMMAND_CLASS_BATTERY, COMMAND_CLASS_WAKE_UP,
// COMMAND_CLASS_VERSION, and the following classes:
#define COMMAND_CLASS_CONFIGURATION 0x70
#define COMMAND_CLASS_SENSOR_MULTILEVEL 0x31
#define COMMAND_CLASS_MULTI_INSTANCE 0x60

using namespace OpenZWave;

static uint32 g_homeId = 0;
static uint8 AlDWSensorId = 0;
static uint8 Hsm100SensorId = 0;
static uint8 ZstickId = 0;
static bool   g_initFailed = false;

typedef struct
{
	uint32			m_homeId;
	uint8			m_nodeId;
	bool			m_polled;
	list<ValueID>	m_values;
}NodeInfo;

static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t  initCond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

// Zeromq initialization for context and publisher
zmq::context_t context(1);
zmq::socket_t publisher(context, ZMQ_PUB);

// Zeromq Functions

//-----------------------------------------------------------------------------
// <sendMessage>
// This function sends the data to the python process using zeromq. 
//-----------------------------------------------------------------------------
void sendMessage(const char *s, float f_val) {

    // char *temp_string = (char *) malloc(30);
    // zmq::message_t message(30);
    // snprintf( temp_string, 30, 
    //         "%s %f", s, f_val);
    // strncpy((char *)message.data(), temp_string, 30);
    // publisher.send(message);
    zmq::message_t message(30);
    sprintf((char *) message.data(), 
            "%s %f ", s, f_val);
    publisher.send(message);
}


//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Return the NodeInfo object associated with this notification
//-----------------------------------------------------------------------------
NodeInfo* GetNodeInfo
(
	Notification const* _notification
)
{
	uint32 const homeId = _notification->GetHomeId();
	uint8 const nodeId = _notification->GetNodeId();
	for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
	{
		NodeInfo* nodeInfo = *it;
		if( ( nodeInfo->m_homeId == homeId ) && ( nodeInfo->m_nodeId == nodeId ) )
		{
			return nodeInfo;
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// <parseHSM100>
// Parses the HSM100 ValueChanged for luminance, temperature, motion, etc.
//-----------------------------------------------------------------------------
void parseHSM100(ValueID value_id) {

    // Initialize Variables
    bool success = false;
    bool bool_value = false;
    uint8 byte_value = 0;
    float float_value = 0.0;
    int32 int_value = 0;

    // Perform action based on CommandClassID
    // For HSM-100, the following Classes need to be taken care of:
    // 1. COMMAND_CLASS_BASIC (0x20)
    // 2. COMMAND_CLASS_WAKE_UP (0x84)
    // 3. COMMAND_CLASS_BATTERY (0x80)
    // 4. COMMAND_CLASS_CONFIGURATION (0x70)
    // 5. COMMAND_CLASS_SENSOR_MULTILEVEL (0x31)
    // 6. COMMAND_CLASS_VERSION (0x86)
    // 7. COMMAND_CLASS_CONFIGURATION (0x70)
    // 8. COMMAND_CLASS_SENSOR_MULTILEVEL (0x31)
    
    // Print value parameters
    /*
       printf("    ValueType: %d\n", (int) value_id.GetType());
       printf("    ValueGenre: %d\n", (int) value_id.GetGenre());
       printf("    Instance: %u\n", (uint8) value_id.GetInstance());
       printf("    ID: %u\n", (uint64) value_id.GetId());
    */

    // Get the Changed Value Based on the type
    switch((int) value_id.GetType()) {
        // See open-zwave/cpp/src/value_classes/ValueID.h for ValueType enum 
        case 0:
            // Boolean Type
            success = Manager::Get()->GetValueAsBool(value_id, &bool_value);
        case 1:
            // Byte Type
            success = Manager::Get()->GetValueAsByte(value_id, &byte_value);
            // printf("Successfully got Value? %s\n", (success)?"Yes":"No");

            break;
        case 2:
            // Float Type
            success = Manager::Get()->GetValueAsFloat(value_id, &float_value);
            break;
        case 3:
            // Int Type
            success = Manager::Get()->GetValueAsInt(value_id, &int_value);
            break;
        default:
            printf("Unrecognized Type: %d\n", (int) value_id.GetType());
            break;
    }
    if(!success)
        printf("Unable to Get the Value\n");
    
    // Output based on the CommandClassId
    switch(value_id.GetCommandClassId()) {
        case COMMAND_CLASS_BASIC:
            //printf("Got COMMAND_CLASS_BASIC!\n");
            
            printf("It has been %u minutes since the last Motion Detected.\n", byte_value);
            break;
        case COMMAND_CLASS_SENSOR_MULTILEVEL:
            //printf("Got COMMAND_CLASS_SENSOR_MULTILEVEL!\n");

            // Report based on instance:
            // 1. General
            // 2. Luminance
            // 3. Temperature
            switch((uint8) value_id.GetInstance()) {
                case 1:
                    // General
                    printf("It has been %f minutes since the last Motion Detected.\n", float_value);
                    sendMessage("Motion", float_value);
                    break;
                case 2:
                    printf("Luminance: %f\n", float_value);
                    sendMessage("Luminance", float_value);
                    break;
                case 3:
                    printf("Temperature: %f\n", float_value);
                    sendMessage("Temperature", float_value);
                    break;

                default:
                    printf("Unrecognized Instance\n");
                    break;
            }
            break;
        case COMMAND_CLASS_CONFIGURATION:
            printf("Configured Value: %u\n", byte_value);
            break;
        case COMMAND_CLASS_WAKE_UP:
            printf("\nGot COMMAND_CLASS_WAKE_UP!\n");
            printf("Wake-up interval: %d seconds\n", int_value);

            break;
        case COMMAND_CLASS_BATTERY:
            printf("Got COMMAND_CLASS_BATTERY!\n");
            printf("\n");
            break;
        case COMMAND_CLASS_VERSION:
            printf("Got COMMAND_CLASS_VERSION!\n");
            break;
        default:
            printf("Got an Unknown COMMAND CLASS!\n");
            break;
    }

}

//-----------------------------------------------------------------------------
// <parseDWSensor>
// Parses the Aeon Labs Door/Window Sensor for basic values (open/closed)
//-----------------------------------------------------------------------------
void parseDWSensor(ValueID value_id) {

    // Initialize Variables
    bool success = false;
    bool bool_value = false;
    uint8 byte_value = 0;
    float float_value = 0;
    int int_value = 0;

    // Get the Changed Value Based on the type
    switch((int) value_id.GetType()) {
        // See open-zwave/cpp/src/value_classes/ValueID.h for ValueType enum 
        case 0:
            // Boolean Type
            success = Manager::Get()->GetValueAsBool(value_id, &bool_value);
        case 1:
            // Byte Type
            success = Manager::Get()->GetValueAsByte(value_id, &byte_value);
            // printf("Successfully got Value? %s\n", (success)?"Yes":"No");
            break;
        case 2:
            // Float Type
            success = Manager::Get()->GetValueAsFloat(value_id, &float_value);
            break;
        case 3:
            // Int Type
            success = Manager::Get()->GetValueAsInt(value_id, &int_value);
            break;
        default:
            printf("Unrecognized Type: %d\n", (int) value_id.GetType());
            break;
    }
    if(!success)
        printf("Unable to Get the Value\n");

    // Perform action based on CommandClassID
    // For Aeon Labs Door/Window Sensor, there are 3 Class to take care of:
    // 1. COMMAND_CLASS_BASIC (0x20)
    // 2. COMMAND_CLASS_SENSOR_BINARY (0x30)
    // 3. COMMAND_CLASS_WAKE_UP (0x84)
    // printf("CommandClassID: %x\n", value_id.GetCommandClassId());
    switch(value_id.GetCommandClassId()) {
        case COMMAND_CLASS_BASIC:
            /*
            printf("Got COMMAND_CLASS_BASIC!\n");
            printf("    ValueType: %d\n", (int) value_id.GetType());
            printf("    ValueGenre: %d\n", (int) value_id.GetGenre());
            printf("    Instance: %u\n", (uint8) value_id.GetInstance());
            printf("    ID: %u\n", (uint64) value_id.GetId());
            */

            // COMMAND_CLASS_BASIC gives a boolean specifying if:
            // 0: Door is closed
            // 255: Door is open

            if(byte_value) {
                printf("Door is Open!\n");
                sendMessage("Door", 1.0);
            }
            else {
                printf("Door is Closed!\n");
                sendMessage("Door", 0);
            }

            break;
        case COMMAND_CLASS_SENSOR_BINARY:
            printf("Got COMMAND_CLASS_SENSOR_BINARY!\n");
            break;
        case COMMAND_CLASS_WAKE_UP:
            printf("Got COMMAND_CLASS_WAKE_UP!\n");
            break;
        case COMMAND_CLASS_BATTERY:
            printf("Got COMMAND_CLASS_BATTERY!\n");
            break;
        case COMMAND_CLASS_ALARM:
            printf("Got COMMAND_CLASS_ALARM!\n");
            break;
        case COMMAND_CLASS_VERSION:
            printf("Got COMMAND_CLASS_VERSION!\n");
            break;
        default:
            printf("Got an Unknown COMMAND CLASS!\n");
            break;
    }
}


//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification
(
	Notification const* _notification,
	void* _context
)
{
	// Must do this inside a critical section to avoid conflicts with the main thread
	pthread_mutex_lock( &g_criticalSection );

    // printf("_notification->GetType(): %d\n", _notification->GetType());

	switch( _notification->GetType() )
	{
		case Notification::Type_ValueAdded:
		{
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				// Add the new value to our list
				nodeInfo->m_values.push_back( _notification->GetValueID() );
			}
			break;
		}

		case Notification::Type_ValueRemoved:
		{
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				// Remove the value from out list
				for( list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it )
				{
					if( (*it) == _notification->GetValueID() )
					{
						nodeInfo->m_values.erase( it );
						break;
					}
				}
			}
			break;
		}

		case Notification::Type_ValueChanged:
		{
			// One of the node values has changed
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
                // printf("Got a ValueChanged!\n");

                // printf("HomeId: %u\n", nodeInfo->m_homeId);
                // printf("nodeID: %u\n", nodeInfo->m_nodeId);

                // ValueID of value involved
                ValueID value_id = _notification->GetValueID();

                //printf("    ValueType: %d\n", (int) value_id.GetType());
                //printf("    ValueGenre: %d\n", (int) value_id.GetGenre());
                //printf("    Instance: %u\n", (uint8) value_id.GetInstance());
                //printf("    ID: %u\n", (uint64) value_id.GetId());

                //printf("ValueID->CommandClassID: %x\n", value_id.GetCommandClassId());


                /*
				for( list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it )
				{
					if(it->GetNodeId() == 8) 
                    {
                        if(it->GetCommandClassId() == COMMAND_CLASS_CONFIGURATION) 
                        {

                            if(it->GetIndex() == 6) 
                            {
                                printf("    ValueType: %d\n", (int) it->GetType());
                                printf("    ValueGenre: %d\n", (int) it->GetGenre());
                                printf("    Instance: %u\n", (uint8) it->GetInstance());
                                printf("    ID: %u\n", (uint64) it->GetId());

                                uint8 byte_value = 0;
                                bool success = Manager::Get()->GetValueAsByte(*it, &byte_value);
                                printf("On Value: %u\n", byte_value);

                            }


                        }

					}
				}
                */

                // Initialize values
                //list<ValueID> valueIDList = nodeInfo->m_values;

                // Perform different actions based on which node
                switch(nodeInfo->m_nodeId) {
                    case 8: 
                        // Read the values in the node information
                        //for( list<ValueID>::iterator it = valueIDList.begin(); it != valueIDList.end(); ++it) {

                        parseHSM100(value_id);
                        break;
                    case 6:
                        // printf("Got a value from Node 6!\n");
                        parseDWSensor(value_id);
                        break;
                    default:
                        printf("Unknown Node\n");
                        break;
                }
                // printf("Finished ValueChanged\n");
                // Jason Tsao Changes End
            }
            break;
        }

		case Notification::Type_Group:
		{
			// One of the node's association groups has changed
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				nodeInfo = nodeInfo;		// placeholder for real action
			}
			break;
		}

		case Notification::Type_NodeAdded:
		{
			// Add the new node to our list
			NodeInfo* nodeInfo = new NodeInfo();
			nodeInfo->m_homeId = _notification->GetHomeId();
			nodeInfo->m_nodeId = _notification->GetNodeId();
			nodeInfo->m_polled = false;		
			g_nodes.push_back( nodeInfo );

            Manager::Get()->AddAssociation(nodeInfo->m_homeId, nodeInfo->m_nodeId, 1, 1);
			break;
		}

		case Notification::Type_NodeRemoved:
		{
			// Remove the node from our list
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
			for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
			{
				NodeInfo* nodeInfo = *it;
				if( ( nodeInfo->m_homeId == homeId ) && ( nodeInfo->m_nodeId == nodeId ) )
				{
					g_nodes.erase( it );
					delete nodeInfo;
					break;
				}
			}
			break;
		}

		case Notification::Type_NodeEvent:
		{
			// We have received an event from the node, caused by a
			// basic_set or hail message.
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
                // Jason Tsao Changes
                
                // printf("Got a NodeEvent!\n");

                printf("nodeID: %u\n", nodeInfo->m_nodeId);

                // ValueID of value involved
                ValueID value_id = _notification->GetValueID();

                // Perform different actions based on which node
                switch(nodeInfo->m_nodeId) {
                    case 6:
                    // Read the values in the node information
                    //list<ValueID> valueIDList = nodeInfo->m_values;
                    //for( list<ValueID>::iterator it = valueIDList.begin(); it != valueIDList.end(); ++it) {

                    // Perform action based on CommandClassID
                    // For Aeon Labs Door/Window Sensor, there are 3 Class to take care of:
                    // 1. COMMAND_CLASS_BASIC (0x20)
                    // 2. COMMAND_CLASS_SENSOR_BINARY (0x30)
                    // 3. COMMAND_CLASS_WAKE_UP (0x84)
                    /*
                    printf("CommandClassID: %x\n", value_id.GetCommandClassId());
                    switch(value_id.GetCommandClassId()) {
                        case COMMAND_CLASS_BASIC:
                            printf("Got COMMAND_CLASS_BASIC!\n");
                            printf("    ValueType: %d\n", (int) value_id.GetType());
                            printf("    ValueGenre: %d\n", (int) value_id.GetGenre());
                            printf("    Instance: %u\n", (uint8) value_id.GetInstance());
                            printf("    ID: %u\n", (uint64) value_id.GetId());

                            // COMMAND_CLASS_BASIC gives a boolean specifying if:
                            // 0: Door is closed
                            // 255: Door is open
                     */

                            if(_notification->GetEvent()) {
                                printf("Door is Open!\n");
                                sendMessage("Door", 1.0);
                            }
                            else {
                                printf("Door is Closed!\n");
                                sendMessage("Door", 0);
                            }

                            /*
                            break;
                        case COMMAND_CLASS_SENSOR_BINARY:
                            printf("Got COMMAND_CLASS_SENSOR_BINARY!\n");
                            break;
                        case COMMAND_CLASS_WAKE_UP:
                            printf("Got COMMAND_CLASS_WAKE_UP!\n");
                            break;
                        case COMMAND_CLASS_BATTERY:
                            printf("Got COMMAND_CLASS_BATTERY!\n");
                            break;
                        case COMMAND_CLASS_ALARM:
                            printf("Got COMMAND_CLASS_ALARM!\n");
                            break;
                        case COMMAND_CLASS_VERSION:
                            printf("Got COMMAND_CLASS_VERSION!\n");
                            break;
                        default:
                            printf("Got an Unknown COMMAND CLASS!\n");
                            break;
                    }
                    */
                            printf("\n");
                            break;
                        case 8:
                            printf("Received Node Event for Node 8\n");
                            printf("Event value: %u\n", _notification->GetEvent());
                            break;
                        default:
                            printf("Received Node Event for Unknown Node %u", nodeInfo->m_nodeId);
                }


                // printf("Finished NodeEvent\n");
                // Jason Tsao Changes End
			}

			break;
		}

		case Notification::Type_PollingDisabled:
		{
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				nodeInfo->m_polled = false;
			}
			break;
		}

		case Notification::Type_PollingEnabled:
		{
			if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
			{
				nodeInfo->m_polled = true;
			}
			break;
		}

		case Notification::Type_DriverReady:
		{
			g_homeId = _notification->GetHomeId();
			break;
		}

		case Notification::Type_DriverFailed:
		{
			g_initFailed = true;
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_AwakeNodesQueried:
		case Notification::Type_AllNodesQueried:
		{
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_DriverReset:
		case Notification::Type_MsgComplete:
		case Notification::Type_NodeNaming:
		case Notification::Type_NodeProtocolInfo:
        {
            if( NodeInfo* nodeInfo = GetNodeInfo(_notification)) {

                // With all protocol info found, set the sensor ids
                // for the zstick, door/window sensor, and hsm-100
                uint32 homeId = nodeInfo->m_homeId;
                uint8 nodeId = nodeInfo->m_nodeId;

                // printf("Finished protocol info for Node %u\n", nodeId);
                //printf("With type: %s\n", Manager::Get()->GetNodeType(nodeInfo->m_homeId, nodeInfo->m_nodeId));
                string name = Manager::Get()->GetNodeProductName(homeId, nodeId);
                string manufacturer_name = Manager::Get()->GetNodeManufacturerName(homeId, nodeId);
                //printf("    With Name: %s\n", name.c_str());
                //printf("    and Manufacturer: %s\n", manufacturer_name.c_str());
                
                if(name == "Door/Window Sensor" && manufacturer_name == "Aeon Labs") {
                    AlDWSensorId = nodeId;
                }
                else if(name == "HSM100 Wireless Multi-Sensor" && manufacturer_name == "Homeseer") {
                    Hsm100SensorId = nodeId;
                }
                else if(name == "Z-Stick S2" && manufacturer_name == "Aeon Labs") {
                    ZstickId = nodeId;
                }


                //printf("Node Type of node %u: %s\n", nodeInfo->m_nodeId, (Manager::Get()->GetNodeType(nodeInfo->m_homeId, nodeInfo->m_nodeId)));


            }
            break;

        }
		case Notification::Type_NodeQueriesComplete:
		default:
		{
		}
	}

	pthread_mutex_unlock( &g_criticalSection );
}

//-----------------------------------------------------------------------------
// <main>
// Create the driver and then wait
//-----------------------------------------------------------------------------
int main( int argc, char* argv[] )
{
	pthread_mutexattr_t mutexattr;

	pthread_mutexattr_init ( &mutexattr );
	pthread_mutexattr_settype( &mutexattr, PTHREAD_MUTEX_RECURSIVE );
	pthread_mutex_init( &g_criticalSection, &mutexattr );
	pthread_mutexattr_destroy( &mutexattr );

	pthread_mutex_lock( &initMutex );

    // Bind zeromq to tcp port 5556
    publisher.bind("tcp://*:5556");

	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file.  If you leave it NULL 
	// the log file will appear in the program's working directory.
	Options::Create( "../../../../config/", "", "" );
	Options::Get()->AddOptionInt( "SaveLogLevel", LogLevel_Detail );
	Options::Get()->AddOptionInt( "QueueLogLevel", LogLevel_Debug );
	Options::Get()->AddOptionInt( "DumpTrigger", LogLevel_Error );
	Options::Get()->AddOptionInt( "PollInterval", 500 );
	Options::Get()->AddOptionBool( "IntervalBetweenPolls", true );
	Options::Get()->AddOptionBool("ValidateValueChanges", true);

    // Turn off Console Logging
    Options::Get()->AddOptionBool("ConsoleOutput", false);
	Options::Get()->Lock();

	Manager::Create();

	// Add a callback handler to the manager.  The second argument is a context that
	// is passed to the OnNotification method.  If the OnNotification is a method of
	// a class, the context would usually be a pointer to that class object, to
	// avoid the need for the notification handler to be a static.
	Manager::Get()->AddWatcher( OnNotification, NULL );

	// Add a Z-Wave Driver
	// Modify this line to set the correct serial port for your PC interface.

	string port = "/dev/ttyUSB0";
	if ( argc > 1 )
	{
		port = argv[1];
	}
	if( strcasecmp( port.c_str(), "usb" ) == 0 )
	{
		Manager::Get()->AddDriver( "HID Controller", Driver::ControllerInterface_Hid );
	}
	else
	{
		Manager::Get()->AddDriver( port );
	}

	// Now we just wait for either the AwakeNodesQueried or AllNodesQueried notification,
	// then write out the config file.
	// In a normal app, we would be handling notifications and building a UI for the user.
	pthread_cond_wait( &initCond, &initMutex );

	// Since the configuration file contains command class information that is only 
	// known after the nodes on the network are queried, wait until all of the nodes 
	// on the network have been queried (at least the "listening" ones) before
	// writing the configuration file.  (Maybe write again after sleeping nodes have
	// been queried as well.)
	if( !g_initFailed )
	{

		Manager::Get()->WriteConfig( g_homeId );

        uint8 ccId = 0;

		// The section below demonstrates setting up polling for a variable.  In this simple
		// example, it has been hardwired to poll COMMAND_CLASS_BASIC on the each node that 
		// supports this setting.
		pthread_mutex_lock( &g_criticalSection );
		for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
		{
			NodeInfo* nodeInfo = *it;

			// skip the controller (most likely node 1)
			if( nodeInfo->m_nodeId == 1) continue;

			for( list<ValueID>::iterator it2 = nodeInfo->m_values.begin(); it2 != nodeInfo->m_values.end(); ++it2 )
			{
				ValueID v = *it2;
                ccId = v.GetCommandClassId();
				if(ccId == COMMAND_CLASS_SENSOR_MULTILEVEL)
				{
                    // Poll every 5 seconds
					Manager::Get()->EnablePoll( v, 2);		// enables polling with "intensity" of 2, though this is irrelevant with only one value polled
				}
                //else if(nodeInfo->m_nodeId == 8 && ccId == COMMAND_CLASS_WAKE_UP) {
                //    // Set the Wake-up interval
                //    bool success = Manager::Get()->SetValue(v, 360);
                //    printf("Set Wake-up Interval Successfully: %s", (success)?"Yes":"No");
                //}
			}
		}
		pthread_mutex_unlock( &g_criticalSection );

        // Initialize Configuration Parameters
        pthread_mutex_lock( &g_criticalSection );
        // Request and Set the "On Time" Config Param to 1 with index 2 (See zwcfg*.xml)
        Manager::Get()->SetConfigParam(g_homeId, Hsm100SensorId, 2, 1); 
        Manager::Get()->RequestConfigParam(g_homeId, Hsm100SensorId, 2); 

        // Request Sensitivity
        Manager::Get()->RequestConfigParam(g_homeId, Hsm100SensorId, 1);
        pthread_mutex_unlock( &g_criticalSection );


		// If we want to access our NodeInfo list, that has been built from all the
		// notification callbacks we received from the library, we have to do so
		// from inside a Critical Section.  This is because the callbacks occur on other 
		// threads, and we cannot risk the list being changed while we are using it.  
		// We must hold the critical section for as short a time as possible, to avoid
		// stalling the OpenZWave drivers.
		// At this point, the program just waits for 3 minutes (to demonstrate polling),
		// then exits
		// for( int i = 0; i < 60*30; i++ )
        while(1)
		{
			pthread_mutex_lock( &g_criticalSection );
			// but NodeInfo list and similar data should be inside critical section
            
            //Manager::Get()->RefreshNodeInfo(g_homeId, 8);
            //Manager::Get()->RequestNodeDynamic(g_homeId, 8);
			pthread_mutex_unlock( &g_criticalSection );
			sleep(5);
		}

		Driver::DriverData data;
		Manager::Get()->GetDriverStatistics( g_homeId, &data );
		printf("SOF: %d ACK Waiting: %d Read Aborts: %d Bad Checksums: %d\n", data.s_SOFCnt, data.s_ACKWaiting, data.s_readAborts, data.s_badChecksum);
		printf("Reads: %d Writes: %d CAN: %d NAK: %d ACK: %d Out of Frame: %d\n", data.s_readCnt, data.s_writeCnt, data.s_CANCnt, data.s_NAKCnt, data.s_ACKCnt, data.s_OOFCnt);
		printf("Dropped: %d Retries: %d\n", data.s_dropped, data.s_retries);
	}

	// program exit (clean up)
	if( strcasecmp( port.c_str(), "usb" ) == 0 )
	{
		Manager::Get()->RemoveDriver( "HID Controller" );
	}
	else
	{
		Manager::Get()->RemoveDriver( port );
	}
	Manager::Get()->RemoveWatcher( OnNotification, NULL );
	Manager::Destroy();
	Options::Destroy();
	pthread_mutex_destroy( &g_criticalSection );
	return 0;
}
