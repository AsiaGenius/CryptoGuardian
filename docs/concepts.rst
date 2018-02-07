========
Concepts
========

MQTT
====

MQTT is a somewhat simple protocol: it follows a publish/subscriber paradigm
and messages are exchanged using topics. These topics are simple strings such
as ``/admin/cafe/attrs``. A publisher can, well, publish messages by sending them
to a MQTT broker using a particular topic and all the subscribers that are
listening to that topic will receive a copy of the message.

Subscribers can listen not only to specific topics, but also to topics with
wildcards. For instance, one could use a '+' to indicate that any token will
match the subscribed topic, such as ``/admin/+/attrs`` - messages sent to both
``/admin/cafe/attrs`` and ``/admin/4593/attrs``, for instance, will be received by
this subscriber. Another possibility is to create a subscription to all
remainder tokens in the topic, such as ``/admin/#``. All messages sent to topics
beginning with ``/admin/`` will be received by this subscriber.

Kafka
=====

Kafka is, in fact, a project from the `Apache Foundation`_. It is a messaging
system that is similar to MQTT in the sense that both are based on
publisher/subscriber. Kafka is way more complex and robust - it deals with
multiple subscribers belonging to the same group (and performs load-balancing
between them), stores and replays messages, and so on. The side effect is that
its clients are not that simple, which could be a heavy burden for tiny
devices.

.. _Apache Foundation: https://kafka.apache.org