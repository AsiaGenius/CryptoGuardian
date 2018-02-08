========
Install CryptoGuardian 
========

Step #2 Folder Structure
====

2.1 - The Folder Structure is;

.. code-block:: bash
    YourROfolder/GameGuard
    YourROfolder/GameGuard/ring.dll
    YourROfolder/GameGuard/Splash.bmp
    YourROfolder/GameGuard/cg.ini
            

2.2 - Copy GameGuard folder to your RO folder; ``https://github.com/AsiaGenius/CryptoGuardian/shield/GameGuard``

|step1|

2.3 - Open your RO and wait status ``Error! Data corrupted...``

|step2|

2.4 - Go to GameGuard folder and open ``ring-0-logs.txt`` this is your config. 
``Note: Whenever you want to update GRF, CG.INI, HEXED or DATA.INI you should repeat this process.``

|step3|

Save this information, we will use it in the future.

2.5 - Go to website ``cryptoguardian.net``, and register your account. 
``Note: The login form is the same as for registration and password recovery. Easy.``

After registration, it will display this alert;

|step4|

Yes, you need the add-on called metamask. 
This add-on will help us validate day-to-day purchase transactions. 
Do not worry Metamask is famous and used by thousands of users.

|step8|


Install Metamask and create your secure password: https://www.youtube.com/watch?v=Vj9ztSdKSPU

``Alert!`` Once installed and logged in, you can check your status on the network, you are synced! You will need balance to activate your protection.

How to buy ethereum? There are several websites. Or you can request support at Discord.

|step5|

2.6 - After added balance, go to your dashboard. Click ``+`` button and fill informations based in to your ``ring-0-logs.txt.``

|step6|

``Note:`` When you confirm, you will open a payment screen based on Metamask. Confirm payment that should be between $ 2- $ 4. After confirmed, wait a few minutes (10 - 20 minutes) for your newest server to appear!

Press F5 to reload dashboard and wait your server!

|step7|

Done! Your server is up and running. Save the Server Address, it will be important. Your server is now running on node, it will never be destroyed, even if the CryptoGuardian services go offline you will not be affected.

2.7 - Go back to your GameGuard folder, and setup your cg.ini

.. code-block:: bash
    [options]
    hexed=legend.exe    //your hexed name
    instances=1         //Dual client, trial? To one client set 1
    maingrf=data.grf    //your main grf, (default is data.grf)
    ini=DATA.INI        //... '-'

    [hash]
    grfhash=??    //the next step.
    ringHash=9347bd84d964cbbd38cfdb0b41688ae   //your ring hash (check your ring-0-logs.txt)

    [network]
    serveraddress=0xdfbdf41d95a41c9093348b733bfe984af2592b36    //your Server Address (go to dashboard and see your Server Address)

2.8 After setting up, save and open the game! Wait status ``Error! Check GameGuard folder log...``

|step9|

Go to your GameGuard file log (ring-0-logs.txt) and copy your GRF OK hash to your cg.ini

|step10|


Done! Finally, CryptoGuardian is installed on RO Folder! Do not worry this will only happen once.

``Note 1:`` - If you change the GRF you should change the grfhash in cg.ini and then open the game to generate a new cg.ini hash and change in the dashboard.

``Note 2:`` - It is indispensable to use this configuration, as it will eliminate Nodelay threats in your RO.

``Note 3:`` - This protection also ensures that Crash ingame (from spr) will be eliminated by 70%.


.. |step1| image:: https://image.prntscr.com/image/lDl6DCZ_RyOKYQjz714YEg.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step2| image:: https://image.prntscr.com/image/3RuKZQDyQyGeN4Bz0HWcug.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step3| image:: https://image.prntscr.com/image/QG9FWMrXQ-2iIZ8Vy2ANEw.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step4| image:: https://image.prntscr.com/image/_dWWCAj1QheOvrnL7A2ozQ.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step5| image:: https://image.prntscr.com/image/1rCnZOVEQDuhyZkdBV5nQQ.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step6| image:: https://image.prntscr.com/image/isx_r9SfSD_HHD3wLnC_PA.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step7| image:: https://image.prntscr.com/image/-EpQwZWQQWCa65mG3OrLcA.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step8| image:: https://image.prntscr.com/image/-9YR02f9Rx6L48WYQPwglQ.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step9| image:: https://image.prntscr.com/image/XojtYWlmTba_HB07owBjWg.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support
.. |step10| image:: https://image.prntscr.com/image/HahXam7zRuuCEWszCKvKDw.png
   :target: http://docs.cryptoguardian.net/en/latest/index.html#how-to-get-support