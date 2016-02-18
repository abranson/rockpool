//Borrowed from https://github.com/pebble/pypkjs/blob/master/pypkjs/javascript/runtime.py#L17
_jskit.make_proxies = function(proxy, origin, names) {
    names.forEach(function(name) {
        proxy[name] = eval("(function " + name + "() { return origin[name].apply(origin, arguments); })");
    });

    return proxy;
}

_jskit.make_properties = function(proxy, origin, names) {
    names.forEach(function(name) {
        Object.defineProperty(proxy, name, {
            configurable: false,
            enumerable: true,
            get: function() {
                return origin[name];
            },
            set: function(value) {
                origin[name] = value;
            }
        });
    });

    return proxy;
}

Pebble = new (function() {
    _jskit.make_proxies(this, _jskit.pebble,
        ['sendAppMessage', 'showSimpleNotificationOnPebble', 'getAccountToken', 'getWatchToken',
        'addEventListener', 'removeEventListener', 'openURL', 'getTimelineToken', 'timelineSubscribe',
        'timelineUnsubscribe', 'timelineSubscriptions', 'getActiveWatchInfo']
    );
})();

performance = new (function() {
    _jskit.make_proxies(this, _jskit.performance, ['now']);
})();

function XMLHttpRequest() {
    var xhr = _jskit.pebble.createXMLHttpRequest();
    _jskit.make_proxies(this, xhr,
        ['open', 'setRequestHeader', 'overrideMimeType', 'send', 'getResponseHeader',
        'getAllResponseHeaders', 'abort', 'addEventListener', 'removeEventListener']);
    _jskit.make_properties(this, xhr,
        ['readyState', 'response', 'responseText', 'responseType', 'status',
        'statusText', 'timeout', 'onreadystatechange', 'ontimeout', 'onload',
        'onloadstart', 'onloadend', 'onprogress', 'onerror', 'onabort']);

    this.UNSENT = 0;
    this.OPENED = 1;
    this.HEADERS_RECEIVED = 2;
    this.LOADING = 3;
    this.DONE = 4;
}

function setInterval(func, time) {
    return _jskit.timer.setInterval(func, time);
}

function clearInterval(id) {
    _jskit.timer.clearInterval(id);
}

function setTimeout(func, time) {
    return _jskit.timer.setTimeout(func, time);
}

function clearTimeout(id) {
    _jskit.timer.clearTimeout(id);
}

navigator.geolocation = new (function() {
    _jskit.make_proxies(this, _jskit.geolocation,
        ['getCurrentPosition', 'watchPosition', 'clearWatch']
    );
})();

console = new (function() {
    _jskit.make_proxies(this, _jskit.console,
        ['log', 'warn', 'error', 'info']
    );
})();

//It appears that Proxy is not available since Qt is using Javascript v5
/*(function() {
    var proxy = _jskit.make_proxies({}, _jskit.localstorage, ['set', 'has', 'deleteProperty', 'keys', 'enumerate']);
    var methods = _jskit.make_proxies({}, _jskit.localstorage, ['clear', 'getItem', 'setItem', 'removeItem', 'key']);
    proxy.get = function get(p, name) { return methods[name] || _jskit.localstorage.get(p, name); }
    this.localStorage = Proxy.create(proxy);
})();*/

//inspired by https://developer.mozilla.org/en-US/docs/Web/API/Storage/LocalStorage
Object.defineProperty(window, "localStorage", new (function () {
    var storage = {};
    Object.defineProperty(storage, "getItem", {
        value: function (key) {
            var value = null;
            if (key !== undefined && key !== null && storage[key] !== undefined) {
                value = storage[key];
            }

            return value;
        },
        writable: false,
        configurable: false,
        enumerable: false
    });

    Object.defineProperty(storage, "key", {
        value: function (index) {
            return Object.keys(storage)[index];
        },
        writable: false,
        configurable: false,
        enumerable: false
    });

    Object.defineProperty(storage, "setItem", {
        value: function (key, value) {
            if (key !== undefined && key !== null) {
                _jskit.localstorage.setItem(key, value);
                storage[key] = (value && value.toString) ? value.toString() : value;
                return true;
            }
            else {
                return false;
            }
        },
        writable: false,
        configurable: false,
        enumerable: false
    });

    Object.defineProperty(storage, "length", {
        get: function () {
            return Object.keys(storage).length;
        },
        configurable: false,
        enumerable: false
    });

    Object.defineProperty(storage, "removeItem", {
        value: function (key) {
            if (key && storage[key]) {
                _jskit.localstorage.removeItem(key);
                delete storage[key];

                return true;
            }
            else {
                return false;
            }
        },
        writable: false,
        configurable: false,
        enumerable: false
    });

    Object.defineProperty(storage, "clear", {
        value: function (key) {
            for (var key in storage) {
                storage.removeItem(key);
            }

            return true;
        },
        writable: false,
        configurable: false,
        enumerable: false
    });

    this.get = function () {
        return storage;
    };

    this.configurable = false;
    this.enumerable = true;
})());

(function() {
    var keys = _jskit.localstorage.keys();
    for (var index in keys) {
        var value = _jskit.localstorage.getItem(keys[index]);
        localStorage.setItem(keys[index], value);
    }
})();

function WebSocket(url, protocols) {
    var ws = _jskit.pebble.createWebSocket(url, protocols);
    _jskit.make_proxies(this, ws, ['close', 'send']);
    _jskit.make_properties(this, ws,
        ['readyState', 'bufferedAmount', 'onopen', 'onerror', 'onclose', 'onmessage',
        'extensions', 'protocol', 'binaryType']);

    this.CONNECTING = 0;
    this.OPEN = 1;
    this.CLOSING = 2;
    this.CLOSED = 3;
}

//Borrowed from https://github.com/pebble/pypkjs/blob/master/pypkjs/javascript/events.py#L9
Event = function(event_type, event_init_dict) {
    var self = this;
    this.stopPropagation = function() {};
    this.stopImmediatePropagation = function() { self._aborted = true; }
    this.preventDefault = function() { self.defaultPrevented = true; }
    this.initEvent = function(event_type, bubbles, cancelable) {
        self.type = event_type;
        self.bubbles = bubbles;
        self.cancelable = cancelable
    };

    if(!event_init_dict) event_init_dict = {};
    this.type = event_type;
    this.bubbles = event_init_dict.bubbles || false;
    this.cancelable = event_init_dict.cancelable || false;
    this.defaultPrevented = false;
    this.target = null;
    this.currentTarget = null;
    this.eventPhase = 2;
    this._aborted = false;
};
Event._init = function(event_type, event_init_dict) {
    //Convenience function to call from the engine
    return new Event(event_type, event_init_dict)
};

Event.NONE = 0;
Event.CAPTURING_PHASE = 1;
Event.AT_TARGET = 2;
Event.BUBBLING_PHASE = 3;

//Borrowed from https://github.com/pebble/pypkjs/blob/master/pypkjs/javascript/ws.py#L14
CloseEvent = function(wasClean, code, reason, eventInitDict) {
    Event.call(this, "close", eventInitDict);

    Object.defineProperties(this, {
        wasClean: {
            get: function() { return wasClean; },
            enumerable: true,
        },
        code: {
            get: function() { return code; },
            enumerable: true,
        },
        reason: {
            get: function() { return reason; },
            enumerable: true,
        },
    });
};

CloseEvent.prototype = Object.create(Event.prototype);
CloseEvent.prototype.constructor = CloseEvent;
CloseEvent._init = function(wasClean, code, reason) {
    //Convenience function to call from the engine
    return new CloseEvent(wasClean, code, reason)
};

MessageEvent = function(origin, data, eventInitDict) {
    Event.call(this, "message", eventInitDict);

    Object.defineProperties(this, {
        origin: {
            get: function() { return origin; },
            enumerable: true,
        },
        data: {
            get: function() { return data; },
            enumerable: true,
        }
    });
};

MessageEvent.prototype = Object.create(Event.prototype);
MessageEvent.prototype.constructor = MessageEvent;
MessageEvent._init = function(origin, data) {
    //Convenience function to call from the engine
    return new MessageEvent(origin, data)
};