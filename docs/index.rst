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

Essas são todas as features disponíveis atualmente

.. list-table:: Device attributes for iotagent-json
    :header-rows: 1

    * - Feature
      - Description
      - Use Case
    * - GRF Protection
      - Protect all GRFs against edit. When updating some GRF the administrator must update the HASH in the dashboard.
		- GRF Edit
		- NoDelay
		- Animation NoDelay
    * - Packet Encryption
      - When encrypting bot users, bots can not log in. Packet manipulating tools will also be broken.
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