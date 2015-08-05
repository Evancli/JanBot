package com.evanli.janbot;

import android.app.Fragment;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import com.zerokol.views.JoystickView;

import butterknife.Bind;
import butterknife.ButterKnife;
import de.greenrobot.event.EventBus;

import static com.evanli.janbot.MainActivity.*;

/**
 * A placeholder fragment containing a simple view.
 */
public class MainActivityFragment extends Fragment {

    @Bind(R.id.joystick) JoystickView joystick;
    @Bind(R.id.status) TextView statusTextView;
    @Bind(R.id.device) TextView deviceTextView;
    @Bind(R.id.data) TextView textView;

    public class JoystickControlEvent {
        public final double x;
        public final double y;

        public JoystickControlEvent(double x, double y) {
            this.x = x;
            this.y = y;
        }
    }

    public MainActivityFragment() {
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_main, container, false);

        ButterKnife.bind(this, view);

        joystick.setOnJoystickMoveListener(new JoystickView.OnJoystickMoveListener() {
            @Override
            public void onValueChanged(int angle, int power, int direction) {

                double x = power * Math.sin(Math.toRadians(angle)) / 100.0f;
                double y = power * Math.cos(Math.toRadians(angle)) / 100.0f;

                EventBus.getDefault().post(new JoystickControlEvent(x, y));
            }
        }, JoystickView.DEFAULT_LOOP_INTERVAL);

        EventBus.getDefault().register(this);

        return view;
    }

    @Override
    public void onStart() {
        super.onStart();

    }

    @Override
    public void onDestroy() {
        EventBus.getDefault().unregister(this);
        super.onDestroy();
    }

    public void onEvent(MainActivity.DeviceStatusEvent event){
        statusTextView.setText(event.statusMessage);
    }

    public void onEvent(MainActivity.DeviceSelectedEvent event){

        String name = "Unknown Device";

        if (event.deviceName != null) {
            name = event.deviceName;
        }

        deviceTextView.setText(name + " " + event.deviceAddress);

        Log.d("JanBot", "DeviceSelectedEvent: " + event);
    }
}


