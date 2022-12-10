package com.pico.cube_xr;

import android.os.Bundle;

public class MainActivity extends android.app.NativeActivity {
  static {
    // Note: Must handle loading of dependent shared libraries manually before
    // the shared library that depends on them is loaded, since there is not
    // currently a way to specify a shared library dependency for NativeActivity
    // via the manifest meta-data.
    System.loadLibrary("cube_xr");
    System.loadLibrary("pxr_api");
  }

  @Override
  protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

  }

  @Override
  protected void onDestroy() {
    super.onDestroy();
  }

}
