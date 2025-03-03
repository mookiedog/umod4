{
    "eprom": {
        "name": "RP58",
        "info": {
            "models": [
                "RSV 2001"
            ]
        },
        "maps": {
            "info": {
                "exhaust": "stock",
                "airbox": "stock",
                "trimpots": "inactive",     // Need to confirm this: I'm not actually sure if the original RP58 supported trimpots or not!
                "throttlebody": "51mm",
                "heads": "stock",
                "mapselect": [
                    // Map0 is the stock street map
                    {
                        "info": {
                            "name": "Street",
                            "airbox": "restricted",
                            "exhaust": "restricted"
                        }
                    },
                    // Map1 is the off-road race track map
                    {
                        "info": {
                            "name:": "Track",
                            "airbox": "derestricted",
                            "exhaust": "derestricted"
                        }
                    }
                ]
            }
        }
    }
}
