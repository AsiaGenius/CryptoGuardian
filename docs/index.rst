=============
CryptoGuardian - The real protection
=============



CryptoGuardian is a shield for Ragnarok Online and also for some other games that use Sockets.

.. toctree::
   :maxdepth: 2
   :caption: Contents:
   :glob:

   install
   emulator
   dashboard
   manage


Features
================

Check out all available features. There are no additional features. All features are available to all.

.. list-table:: attributes for cryptoguardian
    :header-rows: 1

    * - Feature
      - Description
      - Use Case
    * - GRF Protection
      - Topic to which the device will publish messages.
      - ``/admin/efac/attrs``
    * - Packet Crypt
      - Topic from which the device will accept actuation messages.
      - ``/admin/efac/configuration``
    * - Cheats Log
      - Where can the physical device identifier be located.
      - Check `ID-location structure table`_.
    * - Status Log
      - Instructions to transform the message sent by the device to a simple 
        key-value JSON structure.
      - teste


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