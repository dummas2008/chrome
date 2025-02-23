/**
   * @struct
   * @constructor
   */
  Polymer.IronOverlayManagerClass = function() {
    this._overlays = [];
    // Used to keep track of the last focused node before an overlay gets opened.
    this._lastFocusedNodes = [];

    /**
     * iframes have a default z-index of 100, so this default should be at least
     * that.
     * @private {number}
     */
    this._minimumZ = 101;

    this._backdrops = [];

    this._backdropElement = null;
    Object.defineProperty(this, 'backdropElement', {
      get: function() {
        if (!this._backdropElement) {
          this._backdropElement = document.createElement('iron-overlay-backdrop');
        }
        return this._backdropElement;
      }.bind(this)
    });

    /**
     * The deepest active element.
     * returns {?Node} element the active element
     */
    this.deepActiveElement = null;
    Object.defineProperty(this, 'deepActiveElement', {
      get: function() {
        var active = document.activeElement;
        // document.activeElement can be null
        // https://developer.mozilla.org/en-US/docs/Web/API/Document/activeElement
        while (active && active.root && Polymer.dom(active.root).activeElement) {
          active = Polymer.dom(active.root).activeElement;
        }
        return active;
      }.bind(this)
    });
  };

  /**
   * If a node is contained in an overlay.
   * @private
   * @param {Node} node
   * @returns {Boolean}
   */
  Polymer.IronOverlayManagerClass.prototype._isChildOfOverlay = function(node) {
    while (node && node !== document.body) {
      // Use logical parentNode, or native ShadowRoot host.
      node = Polymer.dom(node).parentNode || node.host;
      // Check if it is an overlay.
      if (node && node.behaviors && node.behaviors.indexOf(Polymer.IronOverlayBehaviorImpl) !== -1) {
        return true;
      }
    }
    return false;
  };

  Polymer.IronOverlayManagerClass.prototype._applyOverlayZ = function(overlay, aboveZ) {
    this._setZ(overlay, aboveZ + 2);
  };

  Polymer.IronOverlayManagerClass.prototype._setZ = function(element, z) {
    element.style.zIndex = z;
  };

  /**
   * track overlays for z-index and focus managemant
   */
  Polymer.IronOverlayManagerClass.prototype.addOverlay = function(overlay) {
    var minimumZ = Math.max(this.currentOverlayZ(), this._minimumZ);
    this._overlays.push(overlay);
    var newZ = this.currentOverlayZ();
    if (newZ <= minimumZ) {
      this._applyOverlayZ(overlay, minimumZ);
    }
    var element = this.deepActiveElement;
    // If already in other overlay, don't reset focus there.
    if (this._isChildOfOverlay(element)) {
      element = null;
    }
    this._lastFocusedNodes.push(element);
  };

  Polymer.IronOverlayManagerClass.prototype.removeOverlay = function(overlay) {
    var i = this._overlays.indexOf(overlay);
    if (i >= 0) {
      this._overlays.splice(i, 1);
      this._setZ(overlay, '');

      var node = this._lastFocusedNodes[i];
      // Focus only if still contained in document.body
      if (overlay.restoreFocusOnClose && node && Polymer.dom(document.body).deepContains(node)) {
        node.focus();
      }
      this._lastFocusedNodes.splice(i, 1);
    }
  };

  Polymer.IronOverlayManagerClass.prototype.currentOverlay = function() {
    var i = this._overlays.length - 1;
    while (this._overlays[i] && !this._overlays[i].opened) {
      --i;
    }
    return this._overlays[i];
  };

  Polymer.IronOverlayManagerClass.prototype.currentOverlayZ = function() {
    return this._getOverlayZ(this.currentOverlay());
  };

  /**
   * Ensures that the minimum z-index of new overlays is at least `minimumZ`.
   * This does not effect the z-index of any existing overlays.
   *
   * @param {number} minimumZ
   */
  Polymer.IronOverlayManagerClass.prototype.ensureMinimumZ = function(minimumZ) {
    this._minimumZ = Math.max(this._minimumZ, minimumZ);
  };

  Polymer.IronOverlayManagerClass.prototype.focusOverlay = function() {
    var current = this.currentOverlay();
    // We have to be careful to focus the next overlay _after_ any current
    // transitions are complete (due to the state being toggled prior to the
    // transition). Otherwise, we risk infinite recursion when a transitioning
    // (closed) overlay becomes the current overlay.
    //
    // NOTE: We make the assumption that any overlay that completes a transition
    // will call into focusOverlay to kick the process back off. Currently:
    // transitionend -> _applyFocus -> focusOverlay.
    if (current && !current.transitioning) {
      current._applyFocus();
    }
  };

  Polymer.IronOverlayManagerClass.prototype.trackBackdrop = function(element) {
    // backdrops contains the overlays with a backdrop that are currently
    // visible
    var index = this._backdrops.indexOf(element);
    if (element.opened && element.withBackdrop) {
      // no duplicates
      if (index === -1) {
        this._backdrops.push(element);
      }
    } else if (index >= 0) {
      this._backdrops.splice(index, 1);
    }
  };

  Polymer.IronOverlayManagerClass.prototype.getBackdrops = function() {
    return this._backdrops;
  };

  /**
   * Returns the z-index for the backdrop.
   */
  Polymer.IronOverlayManagerClass.prototype.backdropZ = function() {
    return this._getOverlayZ(this._overlayWithBackdrop()) - 1;
  };

  /**
   * Returns the first opened overlay that has a backdrop.
   */
  Polymer.IronOverlayManagerClass.prototype._overlayWithBackdrop = function() {
    for (var i = 0; i < this._overlays.length; i++) {
      if (this._overlays[i].opened && this._overlays[i].withBackdrop) {
        return this._overlays[i];
      }
    }
  };

  /**
   * Calculates the minimum z-index for the overlay.
   */
  Polymer.IronOverlayManagerClass.prototype._getOverlayZ = function(overlay) {
    var z = this._minimumZ;
    if (overlay) {
      var z1 = Number(window.getComputedStyle(overlay).zIndex);
      // Check if is a number
      // Number.isNaN not supported in IE 10+
      if (z1 === z1) {
        z = z1;
      }
    }
    return z;
  };

  Polymer.IronOverlayManager = new Polymer.IronOverlayManagerClass();