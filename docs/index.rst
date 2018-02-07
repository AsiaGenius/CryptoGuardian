=============
iotagent-json
=============

|License badge| |Docker badge| |Travis badge|

IoT agents are responsible for receiving messages from physical devices
(directly or through a gateway) and sending them commands in order to configure
them. This iotagent-json, in particular, receives messages via MQTT with JSON
payloads.

.. toctree::
   :maxdepth: 2
   :caption: Contents:
   :glob:

   concepts
   operation
   building-documentation


How does it work
================

iotagent-json depends on two things: a Kafka broker, so that it can receive
messages informing it about new devices (and, in extension, about their updates
and removals), and a MQTT broker, so that it can receive messages from the
devices. It waits for messages sent through these two elements: from the device
manager with a management operation on a device and from the MQTT broker with a
message sent by a device.


How to build
============

As this is a npm-based project, building it is as simple as

.. code-block:: bash

    npm install
    npm run-script build


If everything runs fine, the generated code should be in ``./build`` folder.

How to run
==========

As simple as:

.. code-block:: bash

    npm run-script start ./config.json


Remember that you should already have a Kafka node (with a zookeeper instance)
and a MQTT broker (such as `Eclipse Mosquitto`_)

How do I know if it is working properly?
----------------------------------------

Simply put: you won't. In fact you can implement a simple Kafka publisher to
emulate the behaviour of a device manager instance and a listener to check what
messages it is generating. But it seems easier to get the real components -
they are not that hard to start and to use (given that you use dojot's
`docker-compose`_). Check also `DeviceManager documentation`_ for further
information about how to create a new device.


.. |License badge| image:: https://img.shields.io/badge/license-GPL-blue.svg
   :target: https://opensource.org/licenses/GPL-3.0
.. |Docker badge| image:: https://img.shields.io/docker/pulls/dojot/iotagent-json.svg
   :target: https://hub.docker.com/r/dojot/iotagent-json/
.. |Travis badge| image:: https://travis-ci.org/dojot/iotagent-json.svg?branch=cpqd_master
   :target: https://travis-ci.org/dojot/iotagent-json#


.. _Eclipse mosquitto: https://mosquitto.org
.. _docker-compose: https://github.com/dojot/docker-compose
.. _DeviceManager documentation: http://dojotdocs.readthedocs.io/projects/DeviceManager/en/latest/