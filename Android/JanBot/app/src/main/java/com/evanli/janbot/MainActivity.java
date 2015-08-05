package com.evanli.janbot;

import android.app.Activity;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;

import java.util.List;
import java.util.UUID;

import de.greenrobot.event.EventBus;

public class MainActivity extends Activity {
    private final static String TAG = "JanBot";

    public static final String EXTRAS_DEVICE_NAME = "DEVICE_NAME";
    public static final String EXTRAS_DEVICE_ADDRESS = "DEVICE_ADDRESS";

    public static final UUID Serial_Service_UUID = UUID.fromString("195ae58a-437a-489b-b0cd-b7c9c394bae4");
    public static final UUID RX_Characteristic_UUID = UUID.fromString("5fc569a0-74a9-4fa4-b8b7-8354c86e45a4");
    public static final UUID TX_Characteristic_UUID = UUID.fromString("21819ab0-c937-4188-b0db-b9621e1696cd");

    private BluetoothLeService mBluetoothLeService;
    private boolean mConnected = false;
    private BluetoothGattCharacteristic mNotifyCharacteristic;
    private String mDeviceName;
    private String mDeviceAddress;

    private BluetoothGattCharacteristic mRXCharacteristic;
    private BluetoothGattCharacteristic mTXCharacteristic;

    private boolean receiverRegistered;

    private int mPollingInterval = 2000; // 5 seconds by default, can be changed later
    private Handler mPollingHandler;

    Runnable mPollingTask = new Runnable() {
        @Override
        public void run() {
            dataPollingTask();
            mPollingHandler.postDelayed(mPollingTask, mPollingInterval);
        }
    };
    // Events

    public class DeviceStatusEvent {
        public final String statusMessage;

        public DeviceStatusEvent(String statusMessage) {
            this.statusMessage = statusMessage;
        }
    }
    public class DeviceSelectedEvent {
        public final String deviceName;
        public final String deviceAddress;

        public DeviceSelectedEvent(String deviceName, String deviceAddress) {
            this.deviceName = deviceName;
            this.deviceAddress = deviceAddress;
        }
    }

    // Code to manage Service lifecycle.
    private final ServiceConnection mServiceConnection = new ServiceConnection() {

        @Override
        public void onServiceConnected(ComponentName componentName, IBinder service) {
            mBluetoothLeService = ((BluetoothLeService.LocalBinder) service).getService();
            if (!mBluetoothLeService.initialize()) {
                Log.e(TAG, "Unable to initialize Bluetooth");
                finish();
            }
            // Automatically connects to the device upon successful start-up initialization.
            mBluetoothLeService.connect(mDeviceAddress);
        }

        @Override
        public void onServiceDisconnected(ComponentName componentName) {
            mBluetoothLeService = null;
        }
    };

    // Handles various events fired by the Service.
    // ACTION_GATT_CONNECTED: connected to a GATT server.
    // ACTION_GATT_DISCONNECTED: disconnected from a GATT server.
    // ACTION_GATT_SERVICES_DISCOVERED: discovered GATT services.
    // ACTION_DATA_AVAILABLE: received data from the device.  This can be a result of read
    //                        or notification operations.
    private final BroadcastReceiver mGattUpdateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            final String action = intent.getAction();
            if (BluetoothLeService.ACTION_GATT_CONNECTED.equals(action)) {
                mConnected = true;
                updateConnectionState(R.string.connected);
//                invalidateOptionsMenu();
            } else if (BluetoothLeService.ACTION_GATT_DISCONNECTED.equals(action)) {
                mConnected = false;
                updateConnectionState(R.string.disconnected);
//                invalidateOptionsMenu();
//                clearUI();
            } else if (BluetoothLeService.ACTION_GATT_SERVICES_DISCOVERED.equals(action)) {
                // Show all the supported services and characteristics on the user interface.
//                displayGattServices(mBluetoothLeService.getSupportedGattServices());
                bindSerialCharacteristics();
            } else if (BluetoothLeService.ACTION_DATA_AVAILABLE.equals(action)) {
//                displayData(intent.getStringExtra(BluetoothLeService.EXTRA_DATA));
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        EventBus.getDefault().register(this);

        receiverRegistered = false;

        mPollingHandler = new Handler();

        setContentView(R.layout.activity_main);
    }


    @Override
    public void onStart() {
        super.onStart();

    }

    @Override
    protected void onPause() {
        super.onPause();

        if (receiverRegistered) unregisterReceiver(mGattUpdateReceiver);
        receiverRegistered = false;
    }

    @Override
    public void onStop() {

        super.onStop();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        unbindService(mServiceConnection);
        mBluetoothLeService = null;
        EventBus.getDefault().unregister(this);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);

        menu.findItem(R.id.menu_scan).setVisible(false);
        menu.findItem(R.id.menu_stop).setVisible(false);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        if (id == R.id.action_scan) {
            final Intent intent = new Intent(this, DeviceScanActivity.class);
            startActivityForResult(intent, 0);

            return true;
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (resultCode == RESULT_OK) {
            mDeviceName = data.getStringExtra(EXTRAS_DEVICE_NAME);
            mDeviceAddress = data.getStringExtra(EXTRAS_DEVICE_ADDRESS);

            EventBus.getDefault().post(new DeviceSelectedEvent(mDeviceName, mDeviceAddress));

            if (mDeviceAddress != null) {
                startBLEConnection();
            }
            Log.d(TAG, "mDeviceName: " + mDeviceName + " mDeviceAddress: " + mDeviceAddress);
        }
    }

    public void onEvent(MainActivityFragment.JoystickControlEvent event) {
        Log.d(TAG, "x:" + String.valueOf(event.x) + " y:" + String.valueOf(event.y));

        if(mRXCharacteristic != null) {
            byte flagsByte = 0x00;

//            flagsByte |= ((byte)xIsNegative & (byte)0xff) << 1;
//            flagsByte |= ((byte)yIsNegative & (byte)0xff) << 2;
//
//            byte x = (byte) Math.floor(Math.abs(event.x * 255));
//            byte y = (byte) Math.floor(Math.abs(event.y * 255));

            byte scaledX = (byte)((event.x * 127) + 127);
            byte scaledY = (byte)((event.y * 127) + 127);

            byte[] value = new byte[4];
            value[0] = (byte) 0x80;
            value[1] = (byte) scaledX;
            value[2] = (byte) scaledY;
            value[3] = (byte) flagsByte;

            Log.d(TAG, "Message:" + Integer.toString((int) value[0]) + " " + Integer.toString((int) value[1]) + " " + Integer.toString((int)value[2]) + " " + Integer.toBinaryString((int)flagsByte) );
            mRXCharacteristic.setValue(value);
            boolean status = mBluetoothLeService.writeGattCharacteristic(mRXCharacteristic);

            if (status) {
                Log.d(TAG, "Success");
            } else {
                Log.d(TAG, "Failed");
            }
        }
    }

    private void startBLEConnection() {
        Intent gattServiceIntent = new Intent(this, BluetoothLeService.class);
        bindService(gattServiceIntent, mServiceConnection, BIND_AUTO_CREATE);

        receiverRegistered = true;
        registerReceiver(mGattUpdateReceiver, makeGattUpdateIntentFilter());
        if (mBluetoothLeService != null) {
            final boolean result = mBluetoothLeService.connect(mDeviceAddress);
            Log.d(TAG, "Connect request result=" + result);
        }
    }

    private void displayGattServices(List<BluetoothGattService> gattServices) {
        if (gattServices == null) return;

        String uuid = null;
        String unknownServiceString = getResources().getString(R.string.unknown_service);
        String unknownCharaString = getResources().getString(R.string.unknown_characteristic);

        // Loops through available GATT Services.
        for (BluetoothGattService gattService : gattServices) {
            Log.d(TAG, "gattService:" + gattService.getUuid());
            List<BluetoothGattCharacteristic> gattCharacteristics =
                    gattService.getCharacteristics();

            for (BluetoothGattCharacteristic gattCharacteristic : gattCharacteristics) {
                uuid = gattCharacteristic.getUuid().toString();

                Log.d(TAG, "    gattCharacteristic:" + uuid);

                List<BluetoothGattDescriptor> gattDescriptors = gattCharacteristic.getDescriptors();

                for (BluetoothGattDescriptor gattDescriptor : gattDescriptors) {

                    Log.d(TAG, "    gattDescriptor:" + gattDescriptor.getUuid() + " " + gattDescriptor.getValue());
                }
            }
        }
    }

    private void bindSerialCharacteristics() {
        BluetoothGattService serialService =  mBluetoothLeService.getGattService(Serial_Service_UUID);

        if (serialService != null) {
            mRXCharacteristic = serialService.getCharacteristic(RX_Characteristic_UUID);
            mTXCharacteristic = serialService.getCharacteristic(TX_Characteristic_UUID);

            Log.d(TAG, "    RXCharacteristic:" + mRXCharacteristic.getUuid());
            Log.d(TAG, "    TXCharacteristic:" + mTXCharacteristic.getUuid());

            startDataPolling();

        }




    }


    private void startDataPolling() {
        mPollingTask.run();
    }

    private void stopDataPolling() {
        mPollingHandler.removeCallbacks(mPollingTask);
    }

    private void dataPollingTask() {
        if(mTXCharacteristic != null) {
            mBluetoothLeService.readCharacteristic(mTXCharacteristic);
            if ( mTXCharacteristic.getValue() != null) {
                Log.d(TAG,"mTXCharacteristic Value:" + mTXCharacteristic.getValue().toString());
            }

        }
    }

    private void updateConnectionState(final int resourceId) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                EventBus.getDefault().post(new DeviceStatusEvent(getResources().getString(resourceId)));
            }
        });
    }


    private static IntentFilter makeGattUpdateIntentFilter() {
        final IntentFilter intentFilter = new IntentFilter();
        intentFilter.addAction(BluetoothLeService.ACTION_GATT_CONNECTED);
        intentFilter.addAction(BluetoothLeService.ACTION_GATT_DISCONNECTED);
        intentFilter.addAction(BluetoothLeService.ACTION_GATT_SERVICES_DISCOVERED);
        intentFilter.addAction(BluetoothLeService.ACTION_DATA_AVAILABLE);
        return intentFilter;
    }




}
