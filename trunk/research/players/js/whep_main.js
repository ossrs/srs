const btns = document.querySelectorAll('.btn');
const toastContainer = document.querySelector('.toast');
const buttonYes = document.querySelector('#buttonsYes');
const buttonNo = document.querySelector('#buttonsNo');
const buttonsContainer = document.querySelector('#buttonsDiv');

const options = {
    regular: {
        heading: 'This is a Regular notification',
        paragraph: 'This toast message notifies you of something. Whatever that may be.',
        svg: ''
    },
    success: {
        heading: 'This is a Success notification',
        paragraph: 'This toast message notifies you of all your great successes. You rock!',
        svg: '<div class="toast__icon"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="m9 20.42l-6.21-6.21l2.83-2.83L9 14.77l9.88-9.89l2.83 2.83z"/></svg></div>'
    },
    error: {
        heading: 'This is an Error notification',
        paragraph: 'This toast message notifies you of an Error. Don\'t worry it\'s probably not your fault.',
        svg: '<div class="toast__icon"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path fill="currentColor" d="M15.1 3.1L12.9.9L8 5.9L3.1.9L.9 3.1l5 4.9l-5 4.9l2.2 2.2l4.9-5l4.9 5l2.2-2.2l-5-4.9z"/></svg></div>'
    },
    warning: {
        heading: 'This is a Warning notification',
        paragraph: 'This toast message notifies you of a Warning. Who knows what might have happened.',
        svg: '<div class="toast__icon"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M11 14V5h2v9zm0 5v-2h2v2z"/></svg></div>'
    }
}

function createToast({type='regular', buttons='false', close='true', heading='', paragraph='', button1='', button2=''}) {
    const html = `
        <div class="toast__message toast__message--${type}">
            ${options[type].svg}
            <div class="toast__text">
                <h3 class="toast__heading">
                    ${heading ? heading : options[type].heading}
                </h3>
                <p>
                    ${paragraph ? paragraph : options[type].paragraph}
                </p>                
            </div>
            ${close === 'true' ? `<span class="toast__close">&times;</span>` : ''}
            
        </div>
    `;
    toastContainer.insertAdjacentHTML('beforeend', html);

    const toast = toastContainer.lastElementChild;
    toast.addEventListener('animationend', () => toast.remove());
}

function resetForm() {
    document.querySelector('#heading').value = '';
    document.querySelector('#paragraph').value = '';
}

btns.forEach(btn => btn.addEventListener('click', function(e) {
    e.preventDefault();
    const close = document.querySelector('input[name=close]:checked').value;
    const heading = document.querySelector('#heading').value;
    const paragraph = document.querySelector('#paragraph').value;
    resetForm();
    createToast({type: this.value, close, heading, paragraph});
}));


