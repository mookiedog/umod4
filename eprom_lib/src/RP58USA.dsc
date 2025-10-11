{
    "eprom": {
        "name": "RP58USA",
        "info": {
            "models": [
                "RSV 2001-2003"
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
                    // Map0 and MAP1 are identical
                    {
                        "info": {
                            "name": "Street",
                            "airbox": "restricted",
                            "exhaust": "restricted"
                        }
                    }
                ]
            }
        }
    }
}
