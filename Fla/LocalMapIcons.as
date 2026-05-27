import skse;

class LocalMapIcons extends MovieClip {

    static var instance;

    /* refs */
    public var WorldMap:MovieClip;
    public var LocalMapMenu:MovieClip;

    private var basePath:String = '../skse/plugins/LocalMapIcons/';
    private var cache:Object;

    function LocalMapIcons() {
        LocalMapIcons.instance = this;
    }

	function onLoad() {
        WorldMap = _parent._parent.WorldMap;
        LocalMapMenu = WorldMap.LocalMapMenu;
        cache = new Object();
        duckPunch();
    }

    function duckPunch() {
        var IconDisplay = LocalMapMenu.IconDisplay;

        IconDisplay.LMI_CreateMarkers = IconDisplay.CreateMarkers;
        IconDisplay.CreateMarkers = CreateMarkers;
    }

    // @override LocalMapMenu.IconDisplay.CreateMarkers
    public function CreateMarkers(): Void {
        this = LocalMapIcons.instance;
        LocalMapMenu.IconDisplay.LMI_CreateMarkers();
        doLocalIcons();
    }

    function doLocalIcons() {
        var markerData:Array = LocalMapMenu.IconDisplay._markerList;
        // markers are rendered in batches, we want to wait until all are done
        if (markerData[markerData.length - 1] === undefined) return;
        // plus wait another frame so MovieClips are in place
        onEnterFrame = function() {
            for (var i = 0; i < markerData.length; i++) {
                var key = markerData[i]._label,
                    result:Array = cache[key];
                if (result === undefined) {
                    var _result:String = _root.GetLocalMarkers(key);
                    if (_result !== undefined && _result !== '') {
                        result = _result.split(',');
                    } else {
                        result = new Array();
                    }
                    cache[key] = result;
                }
                if (result.length) {
// skse.Log('Marker: ' + markerData[i]._label  + '; result: ' + result);
                    replaceMarker(markerData[i].IconClip.iconHolder, result);
                }
            }
            onEnterFrame = null;
        }
    }

    function replaceMarker(iconHolder:MovieClip, data:Array) {
        if (Number(data[0]) !== NaN) {
            // when "name" is a number, it's pointing to an icon from map menu itself
            iconHolder.icon.gotoAndStop(parseInt(data[0]));
        } else {
            iconHolder.icon._visible = false;
            var mc = iconHolder.createEmptyMovieClip('newIcon', iconHolder.getNextHighestDepth()),
                mcl:MovieClipLoader = new MovieClipLoader(),
                targetFrame = parseInt(data[2]);
            mc._xscale = mc._yscale = parseFloat(data[1]) * 100;
            var listener:Object = {};
            listener.onLoadInit = function(target:MovieClip) {
                if (targetFrame !== 1) {
                    target.gotoAndStop(targetFrame);
                }
                target._y -= target._height / 2;
                target._x -= target._width / 2;
            };
            mcl.addListener(listener);
            mcl.loadClip(basePath + data[0], mc);
        }
    }

    function LogObject( obj ) {
        var s = '';
        for ( var i in obj ) {
            s += i + ': ' + obj[i] + ';\n';
        }
        skse.Log(s);
    }
}