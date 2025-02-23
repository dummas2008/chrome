// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Oobe signin screen implementation.
 */

login.createScreen('GaiaSigninScreen', 'gaia-signin', function() {
  // GAIA animation guard timer. Started when GAIA page is loaded
  // (Authenticator 'ready' event) and is intended to guard against edge cases
  // when 'showView' message is not generated/received.
  /** @const */ var GAIA_ANIMATION_GUARD_MILLISEC = 300;

  // Maximum Gaia loading time in seconds.
  /** @const */ var MAX_GAIA_LOADING_TIME_SEC = 60;

  // The help topic regarding user not being in the whitelist.
  /** @const */ var HELP_CANT_ACCESS_ACCOUNT = 188036;

  // Amount of time the user has to be idle for before showing the online login
  // page.
  /** @const */ var IDLE_TIME_UNTIL_EXIT_OFFLINE_IN_MILLISECONDS = 180 * 1000;

  // Approximate amount of time between checks to see if we should go to the
  // online login page when we're in the offline login page and the device is
  // online.
  /** @const */ var IDLE_TIME_CHECK_FREQUENCY = 5 * 1000;

  /**
   * The modes this screen can be in.
   * @enum {integer}
   */
  var ScreenMode = {
    DEFAULT: 0,           // Default GAIA login flow.
    OFFLINE: 1,           // GAIA offline login.
    SAML_INTERSTITIAL: 2  // Interstitial page before SAML redirection.
  };

  return {
    EXTERNAL_API: [
      'loadAuthExtension',
      'doReload',
      'monitorOfflineIdle',
      'updateControlsState',
      'showWhitelistCheckFailedError',
    ],

    /**
     * Saved gaia auth host load params.
     * @type {?string}
     * @private
     */
    gaiaAuthParams_: null,

    /**
     * Current mode of this screen.
     * @type {integer}
     * @private
     */
    screenMode_: ScreenMode.DEFAULT,

    /**
     * Email of the user, which is logging in using offline mode.
     * @type {string}
     */
    email: '',

    /**
     * Timer id of pending load.
     * @type {number}
     * @private
     */
    loadingTimer_: undefined,

    /**
     * Timer id of a guard timer that is fired in case 'showView' message
     * is not received from GAIA.
     * @type {number}
     * @private
     */
    loadAnimationGuardTimer_: undefined,

    /**
     * Whether we've processed 'showView' message - either from GAIA or from
     * guard timer.
     * @type {boolean}
     * @private
     */
    showViewProcessed_: false,

    /**
     * Whether we've processed 'authCompleted' message.
     * @type {boolean}
     * @private
     */
    authCompleted_: false,

    /**
     * Value contained in the last received 'backButton' event.
     * @type {boolean}
     * @private
     */
    lastBackMessageValue_: false,

    /**
     * Whether the dialog could be closed.
     * @type {boolean}
     */
    get closable() {
      return !!$('pod-row').pods.length || this.isOffline();
    },

    /**
     * Returns true if GAIA is at the begging of flow (i.e. the email page).
     * @type {boolean}
     */
    isAtTheBeginning: function() {
      return !this.navigation_.backVisible &&
             !this.isSAML() &&
             !this.classList.contains('whitelist-error') &&
             !this.authCompleted_;
    },

    /**
     * Updates visibility of navigation buttons.
     */
    updateControlsState: function() {
      var isWhitelistError = this.classList.contains('whitelist-error');

      this.navigation_.backVisible =
        this.lastBackMessageValue_ &&
        !isWhitelistError &&
        !this.authCompleted_ &&
        !this.loading &&
        !this.isSAML();

      this.navigation_.refreshVisible =
        !this.closable && this.isAtTheBeginning() &&
        this.screenMode_ != ScreenMode.SAML_INTERSTITIAL;

      this.navigation_.closeVisible =
        !this.navigation_.refreshVisible &&
        !isWhitelistError &&
        !this.authCompleted &&
        this.screenMode_ != ScreenMode.SAML_INTERSTITIAL;

      $('login-header-bar').updateUI_();
    },

    /**
     * SAML password confirmation attempt count.
     * @type {number}
     */
    samlPasswordConfirmAttempt_: 0,

    /**
     * Do we currently have a setTimeout task running that tries to bring us
     * back to the online login page after the user has idled for awhile? If so,
     * then this id will be non-negative.
     */
    tryToGoToOnlineLoginPageCallbackId_: -1,

    /**
     * The most recent period of time that the user has interacted. This is
     * only updated when the offline page is active and the device is online.
     */
    mostRecentUserActivity_: Date.now(),

    /**
     * An element containg navigation buttons.
     */
    navigation_: undefined,

    /** @override */
    decorate: function() {
      this.navigation_ = $('gaia-navigation');

      this.gaiaAuthHost_ = new cr.login.GaiaAuthHost($('signin-frame'));
      this.gaiaAuthHost_.addEventListener(
          'ready', this.onAuthReady_.bind(this));

      var that = this;
      [this.gaiaAuthHost_, $('offline-gaia')].forEach(function(frame) {
        // Ignore events from currently inactive frame.
        var frameFilter = function(callback) {
          return function(e) {
            var isEventOffline = frame === $('offline-gaia');
            if (isEventOffline === that.isOffline())
              callback.call(that, e);
          };
        };

        frame.addEventListener('authCompleted',
                               frameFilter(that.onAuthCompletedMessage_));
        frame.addEventListener('backButton', frameFilter(that.onBackButton_));
        frame.addEventListener('dialogShown', frameFilter(that.onDialogShown_));
        frame.addEventListener('dialogHidden',
                               frameFilter(that.onDialogHidden_));
      });

      this.gaiaAuthHost_.addEventListener(
          'showView', this.onShowView_.bind(this));
      this.gaiaAuthHost_.confirmPasswordCallback =
          this.onAuthConfirmPassword_.bind(this);
      this.gaiaAuthHost_.noPasswordCallback =
          this.onAuthNoPassword_.bind(this);
      this.gaiaAuthHost_.insecureContentBlockedCallback =
          this.onInsecureContentBlocked_.bind(this);
      this.gaiaAuthHost_.missingGaiaInfoCallback =
          this.missingGaiaInfo_.bind(this);
      this.gaiaAuthHost_.samlApiUsedCallback =
          this.samlApiUsed_.bind(this);
      this.gaiaAuthHost_.addEventListener('authDomainChange',
          this.onAuthDomainChange_.bind(this));
      this.gaiaAuthHost_.addEventListener('authFlowChange',
          this.onAuthFlowChange_.bind(this));

      this.gaiaAuthHost_.addEventListener('loadAbort',
        this.onLoadAbortMessage_.bind(this));
      this.gaiaAuthHost_.addEventListener(
          'identifierEntered', this.onIdentifierEnteredMessage_.bind(this));

      this.navigation_.addEventListener('back', function() {
        this.navigation_.backVisible = false;
        this.getSigninFrame_().back();
      }.bind(this));

      this.navigation_.addEventListener('close', function() {
        this.cancel();
      }.bind(this));
      this.navigation_.addEventListener('refresh', function() {
        this.cancel();
      }.bind(this));

      $('gaia-whitelist-error').addEventListener('buttonclick', function() {
         this.showWhitelistCheckFailedError(false);
      }.bind(this));

      $('gaia-whitelist-error').addEventListener('linkclick', function() {
        chrome.send('launchHelpApp', [HELP_CANT_ACCESS_ACCOUNT]);
      });

      // Register handlers for the saml interstitial page events.
      $('saml-interstitial').addEventListener('samlPageNextClicked',
                                              function() {
        this.screenMode = ScreenMode.DEFAULT;
        this.loadGaiaAuthHost_(true /* doSamlRedirect */);
      }.bind(this));
      $('saml-interstitial').addEventListener('samlPageChangeAccountClicked',
                                              function() {
        this.screenMode = ScreenMode.DEFAULT;
        this.loadGaiaAuthHost_(false /* doSamlRedirect */);
      }.bind(this));
    },

    /**
     * Loads the authenticator and updates the UI to reflect the loading state.
     * @param {boolean} doSamlRedirect If the authenticator should do
     *     authentication by automatic redirection to the SAML-based enrollment
     *     enterprise domain IdP.
     */
    loadGaiaAuthHost_: function(doSamlRedirect) {
      this.loading = true;
      this.startLoadingTimer_();

      this.gaiaAuthParams_.doSamlRedirect = doSamlRedirect;
      this.gaiaAuthHost_.load(cr.login.GaiaAuthHost.AuthMode.DEFAULT,
                              this.gaiaAuthParams_);
    },

    /**
     * Header text of the screen.
     * @type {string}
     */
    get header() {
      return loadTimeData.getString('signinScreenTitle');
    },

    /**
     * Returns true if offline version of Gaia is used.
     * @type {boolean}
     */
    isOffline: function() {
      return this.screenMode_ == ScreenMode.OFFLINE;
    },

    /**
     * Sets the current screen mode and updates the visible elements
     * accordingly.
     * @param {integer} value The screen mode.
     */
    set screenMode(value) {
      this.screenMode_ = value;
      switch (this.screenMode_) {
        case ScreenMode.DEFAULT:
          $('signin-frame').hidden = false;
          $('offline-gaia').hidden = true;
          $('saml-interstitial').hidden = true;
          break;
        case ScreenMode.OFFLINE:
          $('signin-frame').hidden = true;
          $('offline-gaia').hidden = false;
          $('saml-interstitial').hidden = true;
          break;
        case ScreenMode.SAML_INTERSTITIAL:
          $('signin-frame').hidden = true;
          $('offline-gaia').hidden = true;
          $('saml-interstitial').hidden = false;
          break;
      }

      chrome.send('updateOfflineLogin', [this.isOffline()]);
      this.updateControlsState();
    },

    /**
     * This enables or disables trying to go back to the online login page
     * after the user is idle for a few minutes, assuming that we're currently
     * in the offline one. This is only applicable when the offline page is
     * currently active. It is intended that when the device goes online, this
     * gets called with true; when it goes offline, this gets called with
     * false.
     */
    monitorOfflineIdle: function(shouldMonitor) {
      var ACTIVITY_EVENTS = ['click', 'mousemove', 'keypress'];
      var self = this;

      // updateActivityTime_ is used as a callback for addEventListener, so we
      // need the exact reference for removeEventListener. Because the callback
      // needs to access the |this| as scoped inside of this function, we create
      // a closure that uses the appropriate |this|.
      //
      // Unfourtantely, we cannot define this function inside of the JSON object
      // as then we have to no way to create to capture the correct |this|
      // reference. We define it here instead.
      if (!self.updateActivityTime_) {
        self.updateActivityTime_ = function() {
          self.mostRecentUserActivity_ = Date.now();
        };
      }

      // Begin monitoring.
      if (shouldMonitor) {
        // If we're not using the offline login page or we're already
        // monitoring, then we don't need to do anything.
        if (!self.isOffline() ||
            self.tryToGoToOnlineLoginPageCallbackId_ !== -1) {
          return;
        }

        self.mostRecentUserActivity_ = Date.now();
        ACTIVITY_EVENTS.forEach(function(event) {
          document.addEventListener(event, self.updateActivityTime_);
        });

        self.tryToGoToOnlineLoginPageCallbackId_ = setInterval(function() {
          // If we're not in the offline page or the signin page, then we want
          // to terminate monitoring.
          if (!self.isOffline() ||
              Oobe.getInstance().currentScreen.id != 'gaia-signin') {
            self.monitorOfflineIdle(false);
            return;
          }

          var idleDuration = Date.now() - self.mostRecentUserActivity_;
          if (idleDuration > IDLE_TIME_UNTIL_EXIT_OFFLINE_IN_MILLISECONDS) {
            self.monitorOfflineIdle(false);
            Oobe.resetSigninUI(true);
          }
        }, IDLE_TIME_CHECK_FREQUENCY);
      }

      // Stop monitoring.
      else {
        // We're not monitoring, so we don't need to do anything.
        if (self.tryToGoToOnlineLoginPageCallbackId_ === -1)
          return;

        ACTIVITY_EVENTS.forEach(function(event) {
          document.removeEventListener(event, self.updateActivityTime_);
        });
        clearInterval(self.tryToGoToOnlineLoginPageCallbackId_);
        self.tryToGoToOnlineLoginPageCallbackId_ = -1;
      }
    },

    /**
     * Shows/hides loading UI.
     * @param {boolean} show True to show loading UI.
     * @private
     */
    showLoadingUI_: function(show) {
      $('gaia-loading').hidden = !show;

      // Only set hidden for offline-gaia or saml-interstitial and not set it on
      // the 'sign-frame' webview element. Setting it on webview not only hides
      // but also affects its loading events.
      if (this.screenMode_ != ScreenMode.DEFAULT)
        this.getSigninFrame_().hidden = show;
      this.getSigninFrame_().classList.toggle('show', !show);
      this.classList.toggle('loading', show);
      this.updateControlsState();
    },

    /**
     * Handler for Gaia loading timeout.
     * @private
     */
    onLoadingTimeOut_: function() {
      if (this != Oobe.getInstance().currentScreen)
        return;
      this.loadingTimer_ = undefined;
      chrome.send('showLoadingTimeoutError');
    },

    /**
     * Clears loading timer.
     * @private
     */
    clearLoadingTimer_: function() {
      if (this.loadingTimer_) {
        clearTimeout(this.loadingTimer_);
        this.loadingTimer_ = undefined;
      }
    },

    /**
     * Sets up loading timer.
     * @private
     */
    startLoadingTimer_: function() {
      this.clearLoadingTimer_();
      this.loadingTimer_ = setTimeout(this.onLoadingTimeOut_.bind(this),
                                      MAX_GAIA_LOADING_TIME_SEC * 1000);
    },

    /**
     * Handler for GAIA animation guard timer.
     * @private
     */
    onLoadAnimationGuardTimer_: function() {
      this.loadAnimationGuardTimer_ = undefined;
      this.onShowView_();
    },

    /**
     * Clears GAIA animation guard timer.
     * @private
     */
    clearLoadAnimationGuardTimer_: function() {
      if (this.loadAnimationGuardTimer_) {
        clearTimeout(this.loadAnimationGuardTimer_);
        this.loadAnimationGuardTimer_ = undefined;
      }
    },

    /**
     * Sets up GAIA animation guard timer.
     * @private
     */
    startLoadAnimationGuardTimer_: function() {
      this.clearLoadAnimationGuardTimer_();
      this.loadAnimationGuardTimer_ = setTimeout(
          this.onLoadAnimationGuardTimer_.bind(this),
          GAIA_ANIMATION_GUARD_MILLISEC);
    },

    /**
     * Whether Gaia is loading.
     * @type {boolean}
     */
    get loading() {
      return !$('gaia-loading').hidden;
    },
    set loading(loading) {
      if (loading == this.loading)
        return;

      this.showLoadingUI_(loading);
    },

    /**
     * Event handler that is invoked just before the frame is shown.
     * @param {string} data Screen init payload. Url of auth extension start
     *                      page.
     */
    onBeforeShow: function(data) {
      this.screenMode = ScreenMode.DEFAULT;
      this.loading = true;
      chrome.send('loginUIStateChanged', ['gaia-signin', true]);
      $('progress-dots').hidden = true;
      $('login-header-bar').signinUIState = SIGNIN_UI_STATE.GAIA_SIGNIN;

      // Ensure that GAIA signin (or loading UI) is actually visible.
      window.requestAnimationFrame(function() {
        chrome.send('loginVisible', ['gaia-loading']);
      });

      // Button header is always visible when sign in is presented.
      // Header is hidden once GAIA reports on successful sign in.
      Oobe.getInstance().headerHidden = false;

      this.lastBackMessageValue_ = false;
      this.updateControlsState();
    },

    getSigninFrame_: function() {
      switch (this.screenMode_) {
        case ScreenMode.DEFAULT:
          return $('signin-frame');
        case ScreenMode.OFFLINE:
          return $('offline-gaia');
        case ScreenMode.SAML_INTERSTITIAL:
          return $('saml-interstitial');
      }
    },

    focusSigninFrame: function() {
      this.getSigninFrame_().focus();
    },

    onAfterShow: function() {
      if (!this.loading)
        this.focusSigninFrame();
    },

    /**
     * Event handler that is invoked just before the screen is hidden.
     */
    onBeforeHide: function() {
      chrome.send('loginUIStateChanged', ['gaia-signin', false]);
      $('login-header-bar').signinUIState = SIGNIN_UI_STATE.HIDDEN;
      $('offline-gaia').switchToEmailCard(false /* animated */);
    },

    /**
     * Loads the authentication extension into the iframe.
     * @param {Object} data Extension parameters bag.
     * @private
     */
    loadAuthExtension: function(data) {
      this.screenMode = data.screenMode;
      this.email = '';
      this.authCompleted_ = false;
      this.lastBackMessageValue_ = false;

      $('login-header-bar').showCreateSupervisedButton =
          data.supervisedUsersCanCreate;
      $('login-header-bar').showGuestButton = data.guestSignin;

      // Reset SAML
      this.classList.toggle('full-width', false);
      $('saml-notice-container').hidden = true;
      this.samlPasswordConfirmAttempt_ = 0;

      // Screen size could have been changed because of 'full-width' classes.
      if (Oobe.getInstance().currentScreen === this)
        Oobe.getInstance().updateScreenSize(this);

      var params = {};
      for (var i in cr.login.GaiaAuthHost.SUPPORTED_PARAMS) {
        var name = cr.login.GaiaAuthHost.SUPPORTED_PARAMS[i];
        if (data[name])
          params[name] = data[name];
      }

      params.isNewGaiaFlow = true;
      params.doSamlRedirect =
          (this.screenMode_ == ScreenMode.SAML_INTERSTITIAL);

      this.gaiaAuthParams_ = params;
      switch (this.screenMode_) {
        case ScreenMode.DEFAULT:
          this.loadGaiaAuthHost_(false /* doSamlRedirect */);
          break;

        case ScreenMode.OFFLINE:
          this.loadOffline(params);
          break;

        case ScreenMode.SAML_INTERSTITIAL:
          $('saml-interstitial').domain = data.enterpriseDomain;
          if (this.loading)
            this.loading = false;
          // This event is for the browser tests.
          $('saml-interstitial').fire('samlInterstitialPageReady');
          break;
      }
      this.updateControlsState();
      chrome.send('authExtensionLoaded');
    },

    /**
     * Whether the current auth flow is SAML.
     */
    isSAML: function() {
       return this.gaiaAuthHost_.authFlow ==
           cr.login.GaiaAuthHost.AuthFlow.SAML;
    },

    /**
     * Invoked when the authDomain property is changed on the GAIA host.
     */
    onAuthDomainChange_: function() {
      $('saml-notice-message').textContent = loadTimeData.getStringF(
          'samlNotice',
          this.gaiaAuthHost_.authDomain);
    },

    /**
     * Invoked when the authFlow property is changed on the GAIA host.
     */
    onAuthFlowChange_: function() {
      var isSAML = this.isSAML();

      this.classList.toggle('full-width', isSAML);
      $('saml-notice-container').hidden = !isSAML;

      if (Oobe.getInstance().currentScreen === this) {
        Oobe.getInstance().updateScreenSize(this);
      }

      this.updateControlsState();
    },

    /**
     * Invoked when the auth host emits 'ready' event.
     * @private
     */
    onAuthReady_: function() {
      this.showViewProcessed_ = false;
      this.startLoadAnimationGuardTimer_();
      this.clearLoadingTimer_();
      this.loading = false;

      // Warm up the user images screen.
      Oobe.getInstance().preloadScreen({id: SCREEN_USER_IMAGE_PICKER});
    },

    /**
     * Invoked when the auth host emits 'dialogShown' event.
     * @private
     */
    onDialogShown_: function() {
      this.navigation_.disabled = true;
    },

    /**
     * Invoked when the auth host emits 'dialogHidden' event.
     * @private
     */
    onDialogHidden_: function() {
      this.navigation_.disabled = false;
    },

    /**
     * Invoked when the auth host emits 'backButton' event.
     * @private
     */
    onBackButton_: function(e) {
      this.getSigninFrame_().focus();
      this.lastBackMessageValue_ = !!e.detail;
      this.updateControlsState();
    },

    /**
     * Invoked when the auth host emits 'showView' event or when corresponding
     * guard time fires.
     * @private
     */
    onShowView_: function(e) {
      if (this.showViewProcessed_)
        return;

      this.showViewProcessed_ = true;
      this.clearLoadAnimationGuardTimer_();
      this.getSigninFrame_().classList.toggle('show', true);
      this.onLoginUIVisible_();
    },

    /**
     * Called when UI is shown.
     * @private
     */
    onLoginUIVisible_: function() {
      // Show deferred error bubble.
      if (this.errorBubble_) {
        this.showErrorBubble(this.errorBubble_[0], this.errorBubble_[1]);
        this.errorBubble_ = undefined;
      }

      chrome.send('loginWebuiReady');
      chrome.send('loginVisible', ['gaia-signin']);
    },

    /**
     * Invoked when the user has successfully authenticated via SAML, the
     * principals API was not used and the auth host needs the user to confirm
     * the scraped password.
     * @param {string} email The authenticated user's e-mail.
     * @param {number} passwordCount The number of passwords that were scraped.
     * @private
     */
    onAuthConfirmPassword_: function(email, passwordCount) {
      this.loading = true;
      Oobe.getInstance().headerHidden = false;

      if (this.samlPasswordConfirmAttempt_ == 0)
        chrome.send('scrapedPasswordCount', [passwordCount]);

      if (this.samlPasswordConfirmAttempt_ < 2) {
        login.ConfirmPasswordScreen.show(
            email,
            this.samlPasswordConfirmAttempt_,
            this.onConfirmPasswordCollected_.bind(this));
      } else {
        chrome.send('scrapedPasswordVerificationFailed');
        this.showFatalAuthError(
            loadTimeData.getString('fatalErrorMessageVerificationFailed'),
            loadTimeData.getString('fatalErrorTryAgainButton'));
      }
      this.classList.toggle('full-width', false);
    },

    /**
     * Invoked when the confirm password screen is dismissed.
     * @private
     */
    onConfirmPasswordCollected_: function(password) {
      this.samlPasswordConfirmAttempt_++;
      this.gaiaAuthHost_.verifyConfirmedPassword(password);

      // Shows signin UI again without changing states.
      Oobe.showScreen({id: SCREEN_GAIA_SIGNIN});
    },

    /**
     * Inovked when the user has successfully authenticated via SAML, the
     * principals API was not used and no passwords could be scraped.
     * @param {string} email The authenticated user's e-mail.
     */
    onAuthNoPassword_: function(email) {
      this.showFatalAuthError(
          loadTimeData.getString('fatalErrorMessageNoPassword'),
          loadTimeData.getString('fatalErrorTryAgainButton'));
      chrome.send('scrapedPasswordCount', [0]);
    },

    /**
     * Invoked when the authentication flow had to be aborted because content
     * served over an unencrypted connection was detected. Shows a fatal error.
     * This method is only called on Chrome OS, where the entire authentication
     * flow is required to be encrypted.
     * @param {string} url The URL that was blocked.
     */
    onInsecureContentBlocked_: function(url) {
      this.showFatalAuthError(
          loadTimeData.getStringF('fatalErrorMessageInsecureURL', url),
          loadTimeData.getString('fatalErrorDoneButton'));
    },

    /**
     * Shows the fatal auth error.
     * @param {string} message The error message to show.
     * @param {string} buttonLabel The label to display on dismiss button.
     */
    showFatalAuthError: function(message, buttonLabel) {
      login.FatalErrorScreen.show(message, buttonLabel, Oobe.showSigninUI);
    },

    /**
     * Show fatal auth error when information is missing from GAIA.
     */
    missingGaiaInfo_: function() {
      this.showFatalAuthError(
          loadTimeData.getString('fatalErrorMessageNoAccountDetails'),
          loadTimeData.getString('fatalErrorTryAgainButton'));
    },

    /**
     * Record that SAML API was used during sign-in.
     */
    samlApiUsed_: function() {
      chrome.send('usingSAMLAPI');
    },

    /**
     * Invoked when auth is completed successfully.
     * @param {!Object} credentials Credentials of the completed authentication.
     * @private
     */
    onAuthCompleted_: function(credentials) {
      if (credentials.useOffline) {
        this.email = credentials.email;
        chrome.send('authenticateUser',
                    [credentials.email,
                     credentials.password]);
      } else if (credentials.authCode) {
        if (credentials.hasOwnProperty('authCodeOnly') &&
            credentials.authCodeOnly) {
          chrome.send('completeAuthenticationAuthCodeOnly',
                      [credentials.authCode]);
        } else {
          chrome.send('completeAuthentication', [
            credentials.gaiaId,
            credentials.email,
            credentials.password,
            credentials.authCode,
            credentials.usingSAML,
            credentials.gapsCookie
          ]);
        }
      } else {
        chrome.send('completeLogin',
                    [credentials.gaiaId,
                     credentials.email,
                     credentials.password,
                     credentials.usingSAML]);
      }

      this.loading = true;

      // Now that we're in logged in state header should be hidden.
      Oobe.getInstance().headerHidden = true;
      // Clear any error messages that were shown before login.
      Oobe.clearErrors();

      this.authCompleted_ = true;
      this.updateControlsState();
    },

    /**
     * Invoked when onAuthCompleted message received.
     * @param {!Object} e Payload of the received HTML5 message.
     * @private
     */
    onAuthCompletedMessage_: function(e) {
      this.onAuthCompleted_(e.detail);
    },

    /**
     * Invoked when onLoadAbort message received.
     * @param {!Object} e Payload of the received HTML5 message.
     * @private
     */
    onLoadAbortMessage_: function(e) {
      this.onWebviewError(e.detail);
    },

    /**
     * Invoked when identifierEntered message received.
     * @param {!Object} e Payload of the received HTML5 message.
     * @private
     */
    onIdentifierEnteredMessage_: function(e) {
      this.onIdentifierEntered(e.detail);
    },

    /**
     * Clears input fields and switches to input mode.
     * @param {boolean} takeFocus True to take focus.
     * @param {boolean} forceOnline Whether online sign-in should be forced.
     * If |forceOnline| is false previously used sign-in type will be used.
     */
    reset: function(takeFocus, forceOnline) {
      // Reload and show the sign-in UI if needed.
      if (takeFocus) {
        if (!forceOnline && this.isOffline()) {
          // Show 'Cancel' button to allow user to return to the main screen
          // (e.g. this makes sense when connection is back).
          Oobe.getInstance().headerHidden = false;
          $('login-header-bar').signinUIState = SIGNIN_UI_STATE.GAIA_SIGNIN;
          // Do nothing, since offline version is reloaded after an error comes.
        } else {
          Oobe.showSigninUI();
        }
      }
    },

    /**
     * Reloads extension frame.
     */
    doReload: function() {
      if (this.screenMode_ != ScreenMode.DEFAULT)
        return;
      this.gaiaAuthHost_.reload();
      this.loading = true;
      this.startLoadingTimer_();
      this.lastBackMessageValue_ = false;
      this.authCompleted_ = false;
      this.updateControlsState();
    },

    /**
     * Shows sign-in error bubble.
     * @param {number} loginAttempts Number of login attemps tried.
     * @param {HTMLElement} content Content to show in bubble.
     */
    showErrorBubble: function(loginAttempts, error) {
      if (this.isOffline()) {
        $('add-user-button').hidden = true;
        // Reload offline version of the sign-in extension, which will show
        // error itself.
        chrome.send('offlineLogin', [this.email]);
      } else if (!this.loading) {
        // TODO(dzhioev): investigate if this branch ever get hit.
        $('bubble').showContentForElement($('gaia-signin-form-container'),
                                          cr.ui.Bubble.Attachment.LEFT,
                                          error);
      } else {
        // Defer the bubble until the frame has been loaded.
        this.errorBubble_ = [loginAttempts, error];
      }
    },

    /**
     * Called when user canceled signin.
     */
    cancel: function() {
      if (!this.navigation_.refreshVisible && !this.navigation_.closeVisible)
        return;

      if (this.closable)
        Oobe.showUserPods();
      else
        Oobe.resetSigninUI(true);
    },

    /**
     * Handler for webview error handling.
     * @param {!Object} data Additional information about error event like:
     * {string} error Error code such as "ERR_INTERNET_DISCONNECTED".
     * {string} url The URL that failed to load.
     */
    onWebviewError: function(data) {
      chrome.send('webviewLoadAborted', [data.error]);
    },

    /**
     * Handler for identifierEntered event.
     * @param {!Object} data The identifier entered by user:
     * {string} accountIdentifier User identifier.
     */
    onIdentifierEntered: function(data) {
      chrome.send('identifierEntered', [data.accountIdentifier]);
    },

    /**
     * Sets enterprise info strings for offline gaia.
     * Also sets callback and sends message whether we already have email and
     * should switch to the password screen with error.
     */
    loadOffline: function(params) {
      this.loading = true;
      this.startLoadingTimer_();
      var offlineLogin = $('offline-gaia');
      offlineLogin.showEnterpriseMessage = !!('enterpriseDomain' in params);
      if ('emailDomain' in params)
        offlineLogin.emailDomain = '@' + params['emailDomain'];
      offlineLogin.setEmail(params.email);
      this.onAuthReady_();
    },

    /**
     * Show/Hide error when user is not in whitelist. When UI is hidden
     * GAIA is reloaded.
     * @param {boolean} show Show/hide error UI.
     * @param {!Object} opt_data Optional additional information.
     */
    showWhitelistCheckFailedError: function(show, opt_data) {
      if (show) {
        var isManaged = opt_data && opt_data.enterpriseManaged;
        $('gaia-whitelist-error').textContent =
            loadTimeData.getValue(isManaged ? 'whitelistErrorEnterprise' :
                                              'whitelistErrorConsumer');
      }

      this.classList.toggle('whitelist-error', show);
      this.loading = !show;

      if (!show)
        Oobe.showSigninUI();

      this.updateControlsState();
    }
  };
});
