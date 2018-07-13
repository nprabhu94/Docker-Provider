/* @migen@ */
#include <MI.h>
#include "Container_ContainerInventory_Class_Provider.h"

#include <map>
#include <set>
#include <string>
#include <syslog.h>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <fstream>
#include "../cjson/cJSON.h"
#include "../dockerapi/DockerRemoteApi.h"
#include "../dockerapi/DockerRestHelper.h"
#include "Container_ContainerInventory_Serialization.h"
#include "Container_ContainerInventory_Validation.h"

using namespace std;

MI_BEGIN_NAMESPACE

class ContainerQuery
{
private:
	///
	/// Seperate the repository, image, and image tag strings
	///
	/// \param[in] id Image ID
	/// \param[in] properties Raw string of form image:imagetag or repository/image:imagetag
	///
	/// \returns vector<string> of length 3 of form [repository, image, imagetag]
	///
	static vector<string> SetImageRepositoryImageTag(string& properties)
	{
		vector<string> result(3, "");

		switch (properties.size())
		{
		default:
		{
			// Find delimiters in the string of format repository/image:imagetag
			int slashLocation = properties.find('/');
			int colonLocation = properties.find(':');

			if ((unsigned)colonLocation != string::npos)
			{
				if ((unsigned)slashLocation >= properties.size())
				{
					// image:imagetag
					result[1] = properties.substr(0, colonLocation);
				}
				else
				{
					// repository/image:imagetag
					result[0] = properties.substr(0, slashLocation);
					result[1] = properties.substr(slashLocation + 1, colonLocation - slashLocation - 1);
				}

				result[2] = properties.substr(colonLocation + 1);
				break;
			}
		}
		case 0:
		{
			// Do not crash the program
			string mylog = "Container image name (" + properties + ") is improperly formed and could not be parsed in SetRepositoryImageTag";
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			myfile << mylog.c_str() << endl;
			myfile.close();
			syslog(LOG_WARNING, "Container image name (%s) is improperly formed and could not be parsed in SetRepositoryImageTag", properties.c_str());
			break;
		}
		}

		return result;
	}

	///
	/// Map the image name, repository, imagetag to ID
	///
	/// /returns Map of values to ID
	///
	static map<string, vector<string> > GenerateImageNameMap()
	{
		map<string, vector<string> > result;

		// Request images
		vector<string> request(1, DockerRestHelper::restDockerImages());
		vector<cJSON*> response = getResponse(request);

		// See http://docs.docker.com/reference/api/Container_remote_api_v1.21/#list-images for example output
		if (!response.empty() && response[0])
		{
			for (int i = 0; i < cJSON_GetArraySize(response[0]); i++)
			{
				cJSON* entry = cJSON_GetArrayItem(response[0], i);

				if (entry)
				{
					cJSON* tags = cJSON_GetObjectItem(entry, "RepoTags");
					string mylog = "Got RepoTags";
					ofstream myfile;
					myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
					myfile << mylog.c_str() << endl;
					myfile.close();
					syslog(LOG_WARNING, "Got RepoTags of size %d", cJSON_GetArraySize(tags));
					if (tags && cJSON_GetArraySize(tags))
					{
						string value = string(cJSON_GetArrayItem(tags, 0)->valuestring);
						result[string(cJSON_GetObjectItem(entry, "Id")->valuestring)] = SetImageRepositoryImageTag(value);
					}
					else
					{
						string myid = cJSON_GetObjectItem(entry, "Id")->valuestring;
						string mylog = "The container has no RepoTags :" + myid;
						ofstream myfile;
						myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
						myfile << mylog.c_str() << endl;
						myfile.close();
						syslog(LOG_INFO, "The container has no RepoTags: %s", cJSON_GetObjectItem(entry, "Id")->valuestring);
					}
				}
				else
				{
					ofstream myfile;
					myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
					myfile << "Attempt in GenerateImageNameMap to get element of image list returned null" << endl;
					myfile.close();
					syslog(LOG_WARNING, "Attempt in GenerateImageNameMap to get element %d of image list returned null", i);
				}
			}

			// Clean up object
			cJSON_Delete(response[0]);
		}
		else
		{
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			myfile << "API call in GenerateImageNameMap to list images returned null" << endl;
			myfile.close();
			syslog(LOG_WARNING, "API call in GenerateImageNameMap to list images returned null");
		}

		return result;
	}

	///
	/// Get information from container config field
	///
	/// \param[in] instance Object representing the container
	/// \param[in] entry JSON from docker inspect
	///
	static void ObtainContainerConfig(Container_ContainerInventory_Class& instance, cJSON* entry)
	{
		cJSON* config = cJSON_GetObjectItem(entry, "Config");

		if (config)
		{
			// Hostname of container
			instance.ContainerHostname_value(cJSON_GetObjectItem(config, "Hostname")->valuestring);

			// Environment variables
			char* env = cJSON_Print(cJSON_GetObjectItem(config, "Env"));
			try {
				int envStringLength = strlen(env);
				if (envStringLength > 300000)
				{
					string stringToTruncate = env;
					string quotestring = "\"";
					string quoteandbracestring = "\"]";
					string quoteandcommastring = "\",";
					string correctedstring;
					stringToTruncate.resize(300000);
					if (stringToTruncate.compare(stringToTruncate.size() - quotestring.size(), quotestring.size(), quotestring) == 0) {
						//std::cout << "stringtotruncate ends with : " << quotestring << endl;
						correctedstring = stringToTruncate + "]";
						ofstream myfile;
						string mylog = "correcting env variable1";
						myfile.open("/var/opt/microsoft/omsagent/log/envtrimmedinventorylogs.txt", std::ios_base::app);
						myfile << mylog.c_str() << endl;
						myfile.close();
						env = const_cast<char *>(correctedstring.c_str());
						//std::cout << "correcting it: " << correctedstring << endl;
					}
					else if (stringToTruncate.compare(stringToTruncate.size() - quoteandbracestring.size(), quoteandbracestring.size(), quoteandbracestring) == 0) {
						//std::cout << "stringtotruncate ends with : " << quoteandbracestring << endl;
						/*std::cout << "do nothing" << endl;
						std::cout << "string is: " << stringToTruncate << endl;*/
						ofstream myfile;
						string mylog = "correcting env variable2";
						myfile.open("/var/opt/microsoft/omsagent/log/envtrimmedinventorylogs.txt", std::ios_base::app);
						myfile << mylog.c_str() << endl;
						myfile.close();
						env = const_cast<char *>(correctedstring.c_str());
					}
					else if (stringToTruncate.compare(stringToTruncate.size() - quoteandcommastring.size(), quoteandcommastring.size(), quoteandcommastring) == 0) {
						//std::cout << "stringtotruncate ends with : " << quoteandcommastring << endl;
						correctedstring = stringToTruncate.substr(0, stringToTruncate.length() - 1);
						correctedstring = correctedstring + "]";
						ofstream myfile;
						string mylog = "correcting env variable3";
						myfile.open("/var/opt/microsoft/omsagent/log/envtrimmedinventorylogs.txt", std::ios_base::app);
						myfile << mylog.c_str() << endl;
						myfile.close();
						env = const_cast<char *>(correctedstring.c_str());
						//std::cout << "correcting it: " << correctedstring;
					}
					else {
						//std::cout << "stringtotruncate doesnt have quote or brace or comma : " << stringToTruncate << endl;
						correctedstring = stringToTruncate + "\"]";
						ofstream myfile;
						string mylog = "correcting env variable4";
						myfile.open("/var/opt/microsoft/omsagent/log/envtrimmedinventorylogs.txt", std::ios_base::app);
						myfile << mylog.c_str() << endl;
						myfile.close();
						env = const_cast<char *>(correctedstring.c_str());
						//std::cout << "corrected string: " << correctedstring << endl;
					}
				}
			}
			catch (std::exception &e) {
				string myexception = e.what();
				string mycontainer = object.ElementName_value().Str();
				string mylog = "Exception while truncating environment variable ";
				ofstream myfile;
				myfile.open("/var/opt/microsoft/omsagent/log/inventoryenvtruncationfailurelogs.txt", std::ios_base::app);
				myfile << mylog.c_str() << endl;
				myfile << myexception.c_str() << endl;
				myfile.close();
			}
			instance.EnvironmentVar_value(strcmp(env, "null") ? env : "");

			// Command
			char *cmd = cJSON_Print(cJSON_GetObjectItem(config, "Cmd"));
			instance.Command_value(cmd);

			cJSON* labels = cJSON_GetObjectItem(config, "Labels");

			// Compose group
			instance.ComposeGroup_value("");

			if (labels)
			{
				cJSON* groupName = cJSON_GetObjectItem(labels, "com.docker.compose.project");

				if (groupName)
				{
					instance.ComposeGroup_value(groupName->valuestring);
				}
			}
			if(env) free(env);
			if(cmd) free(cmd);
		}
		else
		{
			string mystring = cJSON_GetObjectItem(entry, "Id")->valuestring;
			string mylog = "Attempt in ObtainContainerConfig to get container" + mystring + "config information returned null";
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			myfile << mylog.c_str() << endl;
			myfile.close();
			syslog(LOG_WARNING, "Attempt in ObtainContainerConfig to get container %s config information returned null", cJSON_GetObjectItem(entry, "Id")->valuestring);
		}
	}

	///
	/// Get information from container state field
	///
	/// \param[in] instance Object representing the container
	/// \param[in] entry JSON from docker inspect
	///
	static void ObtainContainerState(Container_ContainerInventory_Class& instance, cJSON* entry)
	{
		cJSON* state = cJSON_GetObjectItem(entry, "State");

		if (state)
		{
			int exitCode = cJSON_GetObjectItem(state, "ExitCode")->valueint;

			// Exit codes less than 0 are not supported by the engine
			if (exitCode < 0)
			{
				exitCode = 128;
				string mystring = cJSON_GetObjectItem(entry, "Id")->valuestring;
				string mylog = "Container " + mystring + " returned negative exit code";
				ofstream myfile;
				myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
				myfile << mylog.c_str() << endl;
				myfile.close();
				syslog(LOG_NOTICE, "Container %s returned negative exit code", cJSON_GetObjectItem(entry, "Id")->valuestring);
			}

			instance.ExitCode_value(exitCode);

			if (exitCode)
			{
				// Container failed
				instance.State_value("Failed");
			}
			else
			{
				// Set the Container status : Running/Paused/Stopped
				if (cJSON_GetObjectItem(state, "Running")->valueint)
				{
					// Container running
					if (cJSON_GetObjectItem(state, "Paused")->valueint)
					{
						// Container paused
						instance.State_value("Paused");
					}
					else
					{
						instance.State_value("Running");
					}
				}
				else
				{
					// Container exited
					instance.State_value("Stopped");
				}
			}

			instance.StartedTime_value(cJSON_GetObjectItem(state, "StartedAt")->valuestring);
			instance.FinishedTime_value(cJSON_GetObjectItem(state, "FinishedAt")->valuestring);
		}
		else
		{
			ofstream myfile;
			string mystring = cJSON_GetObjectItem(entry, "Id")->valuestring;
			string mylog = "Attempt in ObtainContainerState to get container " + mystring + " state information returned null";
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			myfile << mylog.c_str() << endl;
			myfile.close();
			syslog(LOG_WARNING, "Attempt in ObtainContainerState to get container %s state information returned null", cJSON_GetObjectItem(entry, "Id")->valuestring);
		}
	}

	///
	/// Get information from container host config field
	///
	/// \param[in] instance Object representing the container
	/// \param[in] entry JSON from docker inspect
	///
	static void ObtainContainerHostConfig(Container_ContainerInventory_Class& instance, cJSON* entry)
	{
		cJSON* hostConfig = cJSON_GetObjectItem(entry, "HostConfig");

		if (hostConfig)
		{
			// Links
			char* links = cJSON_Print(cJSON_GetObjectItem(hostConfig, "Links"));
			instance.Links_value(strcmp(links, "null") ? links : "");

			// Ports
			char* ports = cJSON_Print(cJSON_GetObjectItem(hostConfig, "PortBindings"));
			instance.Ports_value(strcmp(ports, "{\n}") ? ports : "");

			if(links) free(links);
			if(ports) free(ports);
		}
		else
		{
			string mystring = cJSON_GetObjectItem(entry, "Id")->valuestring;
			string mylog = "Attempt in ObtainContainerHostConfig to get container " + mystring + " host config information returned null";
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			myfile << mylog.c_str() << endl;
			myfile.close();
			syslog(LOG_WARNING, "Attempt in ObtainContainerHostConfig to get container %s host config information returned null", cJSON_GetObjectItem(entry, "Id")->valuestring);
		}
	}

	///
	/// Inspect a container and get the necessary data
	///
	/// \param[in] id Container ID
	/// \returns Object representing the container
	///
	static Container_ContainerInventory_Class InspectContainer(string& id, map<string, vector<string> >& nameMap)
	{
		// New inventory entry
		Container_ContainerInventory_Class instance;

		// Inspect container
		vector<string> request(1, DockerRestHelper::restDockerInspect(id));
		vector<cJSON*> response = getResponse(request);

		// See http://docs.docker.com/reference/api/Container_remote_api_v1.21/#inspect-a-container for example output
		if (!response.empty() && response[0])
		{
			instance.InstanceID_value(cJSON_GetObjectItem(response[0], "Id")->valuestring);
			instance.CreatedTime_value(cJSON_GetObjectItem(response[0], "Created")->valuestring);

			char* containerName = cJSON_GetObjectItem(response[0], "Name")->valuestring;

			if (strlen(containerName))
			{
				// Remove the leading / from the name if it exists (this is an API issue)
				instance.ElementName_value(containerName[0] == '/' ? containerName + 1 : containerName);
			}

			string imageId = string(cJSON_GetObjectItem(response[0], "Image")->valuestring);
			instance.ImageId_value(imageId.c_str());

			string mylog = "Getting namemap.count for container " + id;
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			myfile << mylog.c_str() << endl;
			myfile.close();
			syslog(LOG_INFO, "namemap.count for container %s : %d", id.c_str(), nameMap.count(imageId));
			if (nameMap.count(imageId))
			{
				instance.Repository_value(nameMap[imageId][0].c_str());
				instance.Image_value(nameMap[imageId][1].c_str());
				instance.ImageTag_value(nameMap[imageId][2].c_str());
			}

			ObtainContainerConfig(instance, response[0]);
			ObtainContainerState(instance, response[0]);
			ObtainContainerHostConfig(instance, response[0]);

			// Clean up object
			cJSON_Delete(response[0]);
		}
		else
		{
			string mylog = "Attempt in InspectContainer to inspect " + id + " returned null";
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			myfile << mylog.c_str() << endl;
			myfile.close();
			syslog(LOG_WARNING, "Attempt in InspectContainer to inspect %s returned null", id.c_str());
		}

		return instance;
	}

public:
	///
	/// Get information about all containers on the host
	///
	/// \returns Vector containing objects representing each container
	///
	static vector<Container_ContainerInventory_Class> QueryAll()
	{
		openlog("Container_ContainerInventory", LOG_PID | LOG_NDELAY, LOG_LOCAL1);

		// Get computer name
		string hostname = getDockerHostName();

		vector<Container_ContainerInventory_Class> result;

		// Get all current containers
		set<string> containerIds = listContainerSet(true);

		/// Map the image name, repository, imagetag to ID
		map<string, vector<string> > nameMap = GenerateImageNameMap();
		for (set<string>::iterator i = containerIds.begin(); i != containerIds.end(); ++i)
		{
			// Set all data
			string id = string(*i);
			string mylog = "in QueryAll Creating containerinventory instance for containter: " + id;
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			myfile << mylog.c_str() << endl;
			myfile.close();
			syslog(LOG_INFO, "in QueryAll Creating containerinventory instance for containter: %s", id.c_str());
			Container_ContainerInventory_Class instance = InspectContainer(id, nameMap);
			instance.Computer_value(hostname.c_str());

			ContainerInventorySerializer::SerializeObject(instance);
			result.push_back(instance);
		}

		// Find IDs of deleted containers
		ContainerInventoryValidation cv;
		set<string> deleted = cv.GetDeletedContainers(containerIds);
		for (set<string>::iterator i = deleted.begin(); i != deleted.end(); ++i)
		{
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			string mylog = "In deleted containers iterator";
			myfile << mylog.c_str() << endl;
			myfile.close();
			// Putting string(*i) directly in the function call will cause compilation error
			string id = string(*i);
			Container_ContainerInventory_Class instance = ContainerInventorySerializer::DeserializeObject(id);
			instance.State_value("Deleted");

			result.push_back(instance);
		}

		closelog();
		ofstream myfile;
		myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
		string mylog = "Returning results";
		myfile << mylog.c_str() << endl;
		myfile.close();
		return result;
	}
};

#ifdef _MSC_VER
#pragma region
#endif

Container_ContainerInventory_Class_Provider::Container_ContainerInventory_Class_Provider(Module* module) : m_Module(module){}

Container_ContainerInventory_Class_Provider::~Container_ContainerInventory_Class_Provider(){}

void Container_ContainerInventory_Class_Provider::Load(Context& context)
{
	context.Post(MI_RESULT_OK);
}

void Container_ContainerInventory_Class_Provider::Unload(Context& context)
{
	context.Post(MI_RESULT_OK);
}

void Container_ContainerInventory_Class_Provider::EnumerateInstances(Context& context, const String& nameSpace, const PropertySet& propertySet, bool keysOnly, const MI_Filter* filter)
{
	try
	{
		vector<Container_ContainerInventory_Class> queryResult = ContainerQuery::QueryAll();

		for (unsigned i = 0; i < queryResult.size(); i++)
		{
			ofstream myfile;
			myfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
			string mylog = "Posting context for container with following values:";
			myfile << mylog.c_str() << endl;
			string instanceid = queryResult[i].InstanceID_value().Str();
			mylog = "Instance ID: " + instanceid;
			myfile << mylog.c_str() << endl;
			string caption = queryResult[i].Caption_value().Str();
			mylog = "Caption: " + caption;
			myfile << mylog.c_str() << endl;
			string description = queryResult[i].Description_value().Str();
			mylog = "Description: " + description;
			myfile << mylog.c_str() << endl;
			string ElementName = queryResult[i].ElementName_value().Str();
			mylog = "Element name: " + ElementName;
			myfile << mylog.c_str() << endl;
			string CreatedTime = queryResult[i].CreatedTime_value().Str();
			mylog = "CreatedTime: " + CreatedTime;
			myfile << mylog.c_str() << endl;
			string State = queryResult[i].State_value().Str();
			mylog = "State: " + State;
			myfile << mylog.c_str() << endl;
			string StartedTime = queryResult[i].StartedTime_value().Str();
			mylog = "StartedTime: " + StartedTime;
			myfile << mylog.c_str() << endl;
			string FinishedTime = queryResult[i].FinishedTime_value().Str();
			mylog = "FinishedTime:" + FinishedTime;
			myfile << mylog.c_str() << endl;
			string ImageId = queryResult[i].ImageId_value().Str();
			mylog = "ImageId: " + ImageId;
			myfile << mylog.c_str() << endl;
			string Image = queryResult[i].Image_value().Str();
			mylog = "Image: " + Image;
			myfile << mylog.c_str() << endl;
			string Repository = queryResult[i].Repository_value().Str();
			mylog = "Repository: " + Repository;
			myfile << mylog.c_str() << endl;
			string ImageTag = queryResult[i].ImageTag_value().Str();
			mylog = "ImageTag: " + ImageTag;
			myfile << mylog.c_str() << endl;
			string ComposeGroup = queryResult[i].ComposeGroup_value().Str();
			mylog = "ComposeGroup: " + ComposeGroup;
			myfile << mylog.c_str() << endl;
			string ContainerHostname = queryResult[i].ContainerHostname_value().Str();
			mylog = "ContainerHostname" + ContainerHostname;
			myfile << mylog.c_str() << endl;
			string Computer = queryResult[i].Computer_value().Str();
			mylog = "Computer" + Computer;
			myfile << mylog.c_str() << endl;
			string Command = queryResult[i].Command_value().Str();
			mylog = "Command: " + Command;
			myfile << mylog.c_str() << endl;
			string EnvironmentVar = queryResult[i].EnvironmentVar_value().Str();
			mylog = "EnvironmentVar: " + EnvironmentVar;
			myfile << mylog.c_str() << endl;
			string Ports = queryResult[i].Ports_value().Str();
			mylog = "Ports: " + Ports;
			myfile << mylog.c_str() << endl;
			string Links = queryResult[i].Links_value().Str();
			mylog = "Links: " + Links;
			myfile << mylog.c_str() << endl;

			context.Post(queryResult[i]);
			
			mylog = "Context post successful for container with instance id: " + instanceid;
			myfile << mylog.c_str() << endl;
			myfile.close();
		}
		context.Post(MI_RESULT_OK);
	}

	catch (std::exception &e)
	{
		string myexception = e.what();
		string mylog = "Container_ContainerInventory " + myexception;
		ofstream myuberfile;
		myuberfile.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
		myuberfile << mylog.c_str() << endl;
		myuberfile.close();
		syslog(LOG_ERR, "Container_ContainerInventory %s", e.what());
		context.Post(MI_RESULT_FAILED);
	}
	catch (...)
	{
		ofstream myuberfilenew;
		myuberfilenew.open("/var/opt/microsoft/omsagent/log/inventorylogs.txt", std::ios_base::app);
		myuberfilenew << "Container_ContainerInventory Unknown exception" << endl;
		myuberfilenew.close();
		syslog(LOG_ERR, "Container_ContainerInventory Unknown exception");
		context.Post(MI_RESULT_FAILED);
	}
}

void Container_ContainerInventory_Class_Provider::GetInstance(Context& context, const String& nameSpace, const Container_ContainerInventory_Class& instanceName,
	const PropertySet& propertySet)
{
	context.Post(MI_RESULT_NOT_SUPPORTED);
}

void Container_ContainerInventory_Class_Provider::CreateInstance(Context& context, const String& nameSpace, const Container_ContainerInventory_Class& newInstance)
{
	context.Post(MI_RESULT_NOT_SUPPORTED);
}

void Container_ContainerInventory_Class_Provider::ModifyInstance(Context& context, const String& nameSpace, const Container_ContainerInventory_Class& modifiedInstance, const PropertySet& propertySet)
{
	context.Post(MI_RESULT_NOT_SUPPORTED);
}

void Container_ContainerInventory_Class_Provider::DeleteInstance(Context& context, const String& nameSpace, const Container_ContainerInventory_Class& instanceName)
{
	context.Post(MI_RESULT_NOT_SUPPORTED);
}

#ifdef _MSC_VER
#pragma endregion
#endif

MI_END_NAMESPACE
