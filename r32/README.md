# Buy: 
  * 034 housing to oil fitting


# Connectors
  ## MAF
    * 3L3A-12B579-BA
    * https://www.msextra.com/forums/viewtopic.php?t=57942
    * This is for an 80mm housing: 
    ```
        volts flow KG/HR
        0.311 4.83
        0.474 9.28
        0.564 11.09
        0.656 13.08
        0.719 14.89
        0.792 17.28
        0.847 19.09
        0.918 21.88
        0.984 24.52
        1.088 29.34
        1.175 33.8
        1.274 39.42
        1.386 46.66
        1.486 53.87
        1.604 62.92
        1.74 74.09
        1.877 85.88
        2.022 100.38
        2.133 116.63
        2.269 135.55
        2.423 159.02
        2.577 185.44
        2.735 216.32
        2.904 252.66
        3.07 293.72
        3.249 342.42
        3.44 400.41
        3.64 466.52
        3.856 544.7
        4.079 635.91
        4.308 742.09
        4.542 864.63
        4.713 963.29
    ```

  ## ETB

  * source:
    vr6-manuals/2004 Volkswagen Touareg (7LA) V6-3.2L (BAA)/pages/17645.html
  * connector pins 
    * 6x TE/Tyco tin-plated 964275-2
    * https://www.mouser.com/ProductDetail/TE-Connectivity-AMP/964275-2?qs=cBpaxF8cqw%252B61CIf5nbtng%3D%3D
    * also need the little boots

  ```
  Bosch 0 280 750 474 https://www.maxxecu.com/webhelp/wirings-e-throttle_bodies.html

  Pin  Function
  1    Motor -
  2    Sensor GND
  3    +5V power supply
  4    Motor +
  5    TPS 2 (Analog input x)
  6    TPS 1 (Analog input x)
  ```

  ## Fuel injectors 
  * Connector: 4D0971992
  * depin: https://youtu.be/PvNKkD1PFCs?t=695
  * pins: Sumitomo 8240-0263
    * https://www.ksvlooms.com/products/sumitomo-8240-0263-rs-series-socket-terminal-22-20-awg

  ## VVT Connector
  * same as ETB, TE/Tyco 964275-2

  ## Cam connector
  * TE 929939-1

  ## CLT Connector
  * Same as vvt, TE/Tyco 964275-2


  ## Order
    * Injectors 
      * done
    * VVT
      * done
    * MAF
    * Coils
      * done
    * Knock
      * same
    * Crank 
      * done
    * Cam
      * done
    * LSU
      * done
    * Coolant 

    * Oil Pressure
      * done
    * Fuel Pressure
      * done
    * Oil Temp
      * done

# Specs
* Injector flow rate: 270cc
* dead time: guess=1ms
* 


# Pinout
see pinouts.csv


# Tuning
## VVT
https://www.vwvortex.com/threads/24v-vvt-mapping.4051402/


# TODO
* coolant
  * crack pipe
  * loop old oil cooler outles
* oil
  * -10AN fittings
* alternator
  * make the mount
  * measure belt length
    * get belt
* fuel
  * move fuel lines
  * figure out regulator and return
    * https://www.summitracing.com/parts/aei-13129 ?
* ignition
  * mount the coils
  * change the spark plugs
* mounts
  * make the front mount
  * torque everything
* exhaust
  * make it
    * upper flanges
    * lower flanges? or V-band conversion
