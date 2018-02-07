=========
Operation
=========


Configuration
=============

iotagent-json configuration is pretty simple. The main and only configuration
file is ``config.json``, placed at the repository root directory. For
instance, the default configuration file looks like:

.. code-block:: json

    {
      "mqtt": {
        "host": "mqtt",
        "port" : 1883,
        "protocolId": "MQIsdp",
        "protocolVersion": 3,
        "secure": false,
        "tls": {
          "key": "certs/iotagent.key",
          "cert": "certs/iotagent.crt",
          "ca": [ "certs/ca.crt" ],
          "version": "TLSv1_2_method"
        }
      },
      "broker": {
        "host": "zookeeper:2181",
        "type": "kafka",
        "subject": "device-data",
        "contextBroker": "http://data-broker"
      },
      "device_manager": {
        "consumerOptions": {
          "kafkaHost" : "kafka:9092",
          "sessionTimeout": 15000,
          "groupId": "iotagent"
        },
        "inputSubject": "dojot.device-manager.device"
      },
      "tenancy": {
        "manager": "http://auth:5000",
        "subject": "dojot.tenancy",
        "consumerOptions": {
          "kafkaHost" : "kafka:9092",
          "sessionTimeout": 15000,
          "groupId": "iotagent"
        }
      }
    }



There are four things to configure:

- MQTT: where the device messages will come from.

- MQTT Security: if used (and you should be using), these are the things that
  must be configured. They are related to the communication between iotagent-json
  and the physical device.

- Data broker: where to send device information updates. There is support for
  Kafka (sending a message to every component that is interested in device
  updates) and for Orion (context broker from Fiware project).

- Device manager access: how the device manager will send device notifications
  to iotagent (creation, update and removal).

- Tenancy: how iotagent-json will get tenant-related information, such as which
  are the tenants currently configured in dojot.

Check `dojot documentation`_ if you don't know or don't remember all the
components and how and why they communicate to each other.


Receiving messages from DeviceManager via Kafka
===============================================

Messages containing device operations should be in this format:

.. code-block:: json

    {
      "event": "create",
      "meta": {
        "service": "admin"
      },
      "data": {
        "id": "cafe",
        "attrs" : {

        }
      }
    }

These messages are related to device creation, update, removal and actuation.
For creation and update operations, it contains the device data model
to be added or updated. For removal operation, it will contain only the device
ID being removed. The actuation operation will contain all attributes previously
created with their respective values.

The documentation related to this message can be found in `DeviceManager
Messages`_. 


Device configuration for iotagent-json
--------------------------------------

The following device attributes are considered by iotagent-json. All these
attributes are of ``configuration`` type and their values are in
``static_value`` attribute property.

.. list-table:: Device attributes for iotagent-json
    :header-rows: 1

    * - Attribute
      - Description
      - Example
    * - topic
      - Topic to which the device will publish messages.
      - ``/admin/efac/attrs``
    * - topic-config
      - Topic from which the device will accept actuation messages.
      - ``/admin/efac/configuration``
    * - id-location
      - Where can the physical device identifier be located.
      - Check `ID-location structure table`_.
    * - translator
      - Instructions to transform the message sent by the device to a simple 
        key-value JSON structure.
      - .. code-block:: json

            {
              "op": "move",
              "from": "/data/Modbus_Handler/0/bv",
              "path": "/temperature",
              "optional": true
            }
        
        Keep in mind that this JSON should be "stringified", i.e., all special
        caracters should be escaped. 
        
        This follows the `JSON patch`_ definitions with one important
        difference: if the patch can't be applied (because the message has no
        such attribute), the procedure won't fail.


The translator described in the table would move the value from
``/data/Modbus_Handler/0/bv`` to ``/temperature``, transforming the message
published by the device:

.. code-block:: json
  
    {
      "data" : {
        "Modbus_Handler" : {
          "0" : {
            "bv" : 27.5
          }
        }
      }
    }

into:

.. code-block:: json
  
    {
      "temperature" : 27.5
    }

If the device can't be updated to send messages using the identifier specified
by dojot, iotagent-json can be configured to detect whatever "physical" ID
(let's call it as *physical device ID*) this device has in order to properly
map it to the dojot one (let's call it *dojot device ID*). This configuration
is done by the ``id-location`` device attribute, which is described by the
table below. If none is specified, then iotagent-json will assume a default
behavior, which considers the ID as the second token of MQTT topic, such as:
``/admin/efac/attrs`` with physical device ID being ``efac``.

.. list-table:: ID-location structure
    :header-rows: 1

    * - Attribute
      - Description
      - Example
    * - type
      - Where does the device physical ID can be found.
      - Possible values are:

        - ``mqtt-topic``: The physical device ID is in MQTT topic, such as
          /mqtt/admin/**efac**/attrs

        - ``message-attribute``: The physical device ID is somewhere in the
          message which will be sent from the device. An example would be:

          - ``{"attr1" : 10, "device-id" : "efac"}``
    * - attribute_name

      - Which attribute has the physical device ID, if ``id-location`` is
        ``mqtt-message``.

      - ``device-id``, for a message like ``{"attr1" : 10, "device-id" 
        : "efac"}``
    * - regexp
      - Regular expression applyied to MQTT topic or selected attribute in
        order to extract physical device ID.
      - ``\/.*?\/(.*?)\/.*``
        which extracts ``efac`` from ``/admin/efac/attrs``
    * - id
      - The physical device ID
      - BAFE88420 (any identifier specific to a device)
    * - xid
      - Any message attribute that maps directly to these device ID resolution
        instructions.
      - ``/c/devices/mqtt/`` (the topic used by all devices)
    
The ``xid`` attribute should be understood as "I have these instructions for
locating the device ID, but I don't know which one to use for this message -
thus I'll test the ``xid`` attribute from each one of them against it".
Currently, the ``xid`` is the MQTT topic used to publish the message.

Example
*******

The following message serves as an example of a device with all attributes used
by iotagent-json.

.. code-block:: json

    {
      "label": "Thermometer Template",
      "attrs": [
        {
          "label": "translator",
          "type": "configuration",
          "value_type": "string",
          "static_value": "{ \"op\": \"move\", \"from\": \"/data/Coils/e/1/bv\", \"path\": \"/temperature\", \"optional\": true }"
        },
        {
          "label": "id-location",
          "type": "configuration",
          "value_type": "string",
          "static_value": "{\"xid\":\"/agent/main/000BABC80F4A/devinfo\",\"id\":\"000BABC80F4A\",\"type\":\"mqtt-topic\",\"regexp\":\"\\/.*?\\/.*?\\/(.*?)\\/.*\"}"
        },
        {
          "label": "topic",
          "type": "configuration",
          "value_type": "string",
          "static_value": "/agent/main/000BABC80F4A/devinfo"
        },
        {
          "label": "topic-config",
          "type": "configuration",
          "value_type": "string",
          "static_value": "/agent/main/000BABC80F4A/config"
        },
        {
          "label": "temperature",
          "type": "dynamic",
          "value_type": "float"
        },
        {
          "label": "reset",
          "type": "actuator",
          "value_type": "boolean"
        }
      ]
    }

For the sake of readability, below are both values for translator and
id-location, with no escape characters.

translator: 
  .. code-block:: json

      {
        "op": "move",
        "from": "/data/Coils/e/1/bv",
        "path": "/temperature",
        "optional": true
      }

id-location:
  .. code-block:: json
  
      {
        "xid": "/agent/main/000BABC80F4A/devinfo",
        "id": "000BABC80F4A",
        "type": "mqtt-topic",
        "regexp": "\\/.*?\\/.*?\\/(.*?)\\/.*"
      }

These configurations indicate that:

- The device will publish its messages to ``/agent/main/000BABC80F4A/devinfo``
  topic;

- The device will receive commands via MQTT from topic
  ``/agent/main/000BABC80F4A/config``

- Its ID is in MQTT topic, which can be extracted using the regular expression
  ``\/.*?\/.*?\/(.*?)\/.*`` and its ID should match 000BABC80F4A.

-  The message should be transformed from:

  .. code-block:: json
    
      {
        "data" : {
          "Modbus_Handler" : {
            "0" : {
              "bv" : 1234
            }
          }
        }
      }

  into:

  .. code-block:: json
    
      {
        "temperature" : 1234
      }


- These instructions should be applied whenever a message to the topic
  ``/agent/main/000BABC80F4A/devinfo`` is received.



Receiving messages from devices via MQTT
========================================

Any message payload sent to iotagent-json must be in JSON format. Preferably,
they should follow a simple key-value structure, such as:

.. code-block:: json

    {
      "speed": 100.0,
      "weight": 50.2,
      "id": "truck-001"
    }


If not possible, you could make use of ``translator`` attributes so that you
get more flexibility on device message formats.

Example
-------

This example uses ``mosquitto_pub`` tool, available with ``mosquitto_clients``
package. To send a message to iotagent-json via MQTT, just execute this
command:

.. code-block:: bash

    mosquitto_pub -h localhost -t /admin/efac/attrs -m '{"speed" : 10}'

This command will send the message containing one value for attribute
``speed``. The device ID is ``efac``. ``-t`` flag sets the topic to which this
message will be published.

This command assumes that you are running iotagent-json in your machine (it also
works if you use dojot's `docker-compose`_).


.. _DeviceManager Concepts: http://dojotdocs.readthedocs.io/projects/DeviceManager/en/latest/concepts.html
.. _DeviceManager Messages: http://dojotdocs.readthedocs.io/projects/DeviceManager/en/latest/kafka-messages.html
.. _dojot documentation: http://dojotdocs.readthedocs.io/en/latest/
.. _JSON patch: http://jsonpatch.com/
.. _ID-location structure table: #id2
.. _docker-compose: https://github.com/dojot/docker-compose